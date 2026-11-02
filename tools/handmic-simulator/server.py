#!/usr/bin/env python3
"""Loopback-only Web Passport, native BLE bridge and portable backend supervisor."""
import argparse
import base64
from collections import deque
import hashlib
import importlib.util
import http.server
import json
import math
from pathlib import Path
import queue
import socket
import struct
import subprocess
import threading
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
_codec_spec = importlib.util.spec_from_file_location('rf_link_adpcm', HERE / 'adpcm.py')
adpcm = importlib.util.module_from_spec(_codec_spec)
_codec_spec.loader.exec_module(adpcm)


def ws_frame(payload, opcode=1):
    n = len(payload)
    return bytes([0x80 | opcode]) + (bytes([n]) if n < 126 else b'\x7e' + struct.pack('!H', n) if n < 65536 else b'\x7f' + struct.pack('!Q', n)) + payload


def exact(stream, n):
    result = bytearray()
    while len(result) < n:
        part = stream.read(n - len(result))
        if not part:
            raise EOFError
        result.extend(part)
    return bytes(result)


def read_ws(stream):
    first, second = exact(stream, 2)
    if not first & 0x80 or not second & 0x80:
        raise ValueError('fragmented/unmasked client frame')
    n = second & 127
    if n == 126:
        n = struct.unpack('!H', exact(stream, 2))[0]
    elif n == 127:
        n = struct.unpack('!Q', exact(stream, 8))[0]
    if n > 1_048_576:
        raise ValueError('oversize frame')
    mask = exact(stream, 4)
    payload = exact(stream, n)
    return first & 15, bytes(x ^ mask[i % 4] for i, x in enumerate(payload))


def rgb565_png(raw):
    if len(raw) != 240 * 320 * 2:
        raise ValueError('framebuffer length')
    scan = bytearray()
    for y in range(320):
        scan.append(0)
        for x in range(240):
            p = (y * 240 + x) * 2
            color = raw[p] | raw[p + 1] << 8
            scan.extend((((color >> 11) * 255 + 15) // 31, (((color >> 5) & 63) * 255 + 31) // 63, ((color & 31) * 255 + 15) // 31))
    def chunk(k, v):
        return struct.pack('!I', len(v)) + k + v + struct.pack('!I', zlib.crc32(k + v))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', 240, 320, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b'')


class ArrivalMetrics:
    def __init__(self):
        self.total = self.invalid = self.missing = self.duplicate = self.reordered = self.discontinuities = 0
        self.last_seq = self.last_ns = self.stream = None
        self.intervals = deque(maxlen=1000)
        self.arrivals = deque(maxlen=2000)
        self.sample_squares = self.samples = self.clipped = 0
        self.lock = threading.Lock()

    def receive(self, data, arrival_ns=None):
        with self.lock:
            self.total += 1
            now = time.monotonic()
            self.arrivals.append(now)
            if len(data) < 16 or data[0] != 1 or data[1] & ~1 or (len(data),data[2],data[3]) != (100,2,160):
                self.invalid += 1
                return None
            try:
                pcm = adpcm.decode(data[16:])
            except ValueError:
                self.invalid += 1
                return None
            sid, stream, seq = struct.unpack_from('<III', data, 4)
            key = (sid, stream)
            timestamp = arrival_ns if isinstance(arrival_ns, int) and arrival_ns > 0 else time.monotonic_ns()
            if key != self.stream:
                self.stream, self.last_seq, self.last_ns = key, None, None
            if self.last_ns is not None and timestamp >= self.last_ns:
                self.intervals.append((timestamp - self.last_ns) / 1e6)
            if self.last_seq is not None:
                if seq == self.last_seq:
                    self.duplicate += 1
                elif seq < self.last_seq:
                    self.reordered += 1
                elif seq > self.last_seq + 1:
                    self.missing += seq - self.last_seq - 1
            self.last_seq = max(seq, self.last_seq) if self.last_seq is not None else seq
            self.last_ns = timestamp
            self.discontinuities += bool(data[1] & 1)
            samples = struct.unpack('<%dh' % (len(pcm)//2), pcm)
            self.sample_squares += sum(v * v for v in samples)
            self.samples += len(samples)
            self.clipped += sum(abs(v) >= 32700 for v in samples)
            return pcm

    def snapshot(self):
        with self.lock:
            intervals = sorted(self.intervals)
            def percentile(f):
                return round(intervals[min(len(intervals) - 1, int((len(intervals) - 1) * f))], 2) if intervals else 0
            return {'packets': self.total, 'invalid': self.invalid, 'sequence_missing': self.missing,
                    'duplicates': self.duplicate, 'reordered': self.reordered, 'discontinuities': self.discontinuities,
                    'packets_per_second': sum(t >= time.monotonic() - 1 for t in self.arrivals),
                    'interval_p50_ms': percentile(.5), 'interval_p95_ms': percentile(.95), 'interval_max_ms': percentile(1),
                    'bursts_under_1ms': sum(v < 1 for v in intervals),
                    'rms': round(math.sqrt(self.sample_squares / self.samples)) if self.samples else 0,
                    'clip_percent': round(100 * self.clipped / self.samples, 3) if self.samples else 0}


class Peer:
    def __init__(self, sock, bridge=False):
        self.sock, self.bridge = sock, bridge
        self.out = queue.Queue(maxsize=256)
        self.alive = True
        self.dropped = 0
        self.latest_frame = None
        self.frame_lock = threading.Lock()
        threading.Thread(target=self.writer, daemon=True).start()

    def send(self, value, opcode=1):
        if not self.alive:
            return
        if not isinstance(value, bytes):
            value = json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode()
        if opcode == 2 and value[:1] == b'\x01':
            with self.frame_lock:
                self.latest_frame = ws_frame(value, opcode)
            return
        try:
            self.out.put_nowait(ws_frame(value, opcode))
        except queue.Full:
            self.dropped += 1
            if self.bridge:  # No silent loss of control events to the peripheral.
                self.close()

    def writer(self):
        try:
            while self.alive:
                with self.frame_lock:
                    frame, self.latest_frame = self.latest_frame, None
                if frame:
                    self.sock.sendall(frame)
                try:
                    payload = self.out.get(timeout=.02)
                except queue.Empty:
                    continue
                self.sock.sendall(payload)
        except OSError:
            self.close()

    def close(self):
        self.alive = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


class Simulator:
    def __init__(self, backend=True):
        self.lock = threading.RLock()
        self.input_lock = threading.Lock()
        self.peers = set()
        self.bridge = None
        self.key_owners = {}
        self.native_metrics = {}
        self.cached = {}
        self.frame = b''
        self.last_metrics = {}
        self.bridge_state = {'type': 'bridge_state', 'state': 'disconnected'}
        self.events = deque(maxlen=200)
        self.arrivals = ArrivalMetrics()
        self.process = None
        self.backend_alive = False
        self.failure_reported = False
        self.last_raw_stream = None
        self.last_raw_at = 0
        self.last_playback_stream = None
        self.running = True
        self.started = time.monotonic()
        self.log_file = None
        if backend:
            folder = ROOT / 'artifacts/handmic-simulator'
            folder.mkdir(parents=True, exist_ok=True)
            self.log_file = (folder / 'backend.log').open('a')
            command = ['docker', 'compose', 'run', '--rm', '--no-deps', '-T', 'sdk', '/build/handmic-simulator/backend/passport_simulator', '/workspace/artifacts/handmic-simulator/settings.json']
            self.process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log_file, text=True, bufsize=1)
            threading.Thread(target=self.read_backend, daemon=True).start()
        threading.Thread(target=self.metrics_loop, daemon=True).start()

    def backend(self, msg):
        if not self.process or self.process.poll() is not None:
            self.backend_failed()
            return False
        try:
            with self.input_lock:
                self.process.stdin.write(json.dumps(msg, ensure_ascii=False, separators=(',', ':')) + '\n')
                self.process.stdin.flush()
            return True
        except (BrokenPipeError, OSError):
            self.backend_failed()
            return False

    def backend_failed(self):
        with self.lock:
            if self.failure_reported:
                return
            self.failure_reported = True
            self.backend_alive = False
            self.cached.clear()
            self.native_metrics = {}
            self.last_metrics = dict(self.last_metrics, available=False, stale=True, connected=False, native_connected=False, ready=False)
            self.bridge_state = {'type':'bridge_state','state':'error','message':'模拟器后端已停止，请重新启动。'}
            peer = self.bridge
        if peer:
            peer.send({'type':'advertise','enabled':False})
            peer.close()  # Native transport loss stops ads and recreates virtual services.
        self.broadcast({'type':'audio_boundary','source':'all'})
        self.broadcast(self.bridge_state)

    def broadcast(self, value, opcode=1):
        with self.lock:
            peers = [p for p in self.peers if p.alive and not p.bridge]
        for p in peers:
            p.send(value, opcode)

    def read_backend(self):
        try:
            while True:
                line = self.process.stdout.readline(1_048_577)
                if not line:
                    break
                if not line.startswith('{') or len(line) > 1_048_576:
                    if self.log_file:
                        self.log_file.write(line[:2000]); self.log_file.flush()
                    continue
                try:
                    self.output(json.loads(line))
                except (ValueError, TypeError, KeyError):
                    pass
        finally:
            self.backend_failed()
            self.broadcast({'type': 'event', 'message': '模拟器后端已停止，请查看终端日志。'})

    def output(self, msg):
        kind = msg.get('type')
        with self.lock:
            if self.failure_reported:
                return
        if kind in ('characteristic', 'advertise'):
            key = kind + ':' + msg.get('channel', '')
            with self.lock:
                if self.failure_reported:
                    return
                self.cached[key] = msg
                bridge = self.bridge
            if bridge:
                bridge.send(msg)
        elif kind in ('ble_notify', 'reset_link'):
            with self.lock:
                bridge = self.bridge
            if bridge:
                bridge.send(msg)
        elif kind == 'frame':
            raw = bytes.fromhex(msg['hex'])
            if len(raw) != 153600:
                return
            with self.lock:
                self.frame = raw
            self.broadcast(b'\x01' + raw, 2)
            return
        elif kind == 'playback':
            raw = bytes.fromhex(msg['hex'])
            if len(raw) == 320:
                self.broadcast(b'\x03' + raw, 2)
            return
        elif kind == 'metrics':
            with self.lock:
                if self.failure_reported:
                    return
                self.last_metrics = msg
                self.backend_alive = True
            return
        elif kind == 'event':
            with self.lock:
                self.events.append({'at_s': round(time.monotonic() - self.started, 2), 'message': str(msg.get('message', ''))[:500]})
        self.broadcast(msg)

    def incoming(self, peer, msg):
        if not isinstance(msg, dict):
            return
        kind = msg.get('type')
        if peer.bridge:
            if kind == 'bridge_metrics':
                self.native_metrics = msg
            if kind == 'bridge_state':
                with self.lock:
                    self.bridge_state = msg
                self.broadcast(msg)
            if kind == 'ble_write' and msg.get('channel') == 'speaker':
                try:
                    data = bytes.fromhex(msg.get('hex', ''))
                    stream_key = struct.unpack_from('<II',data,4) if len(data) == 100 else None
                    now = time.monotonic()
                    if stream_key is not None and (stream_key!=self.last_raw_stream or now-self.last_raw_at>.3):
                        self.broadcast({'type':'audio_boundary','source':'raw'})
                    self.last_raw_stream,self.last_raw_at=stream_key,now
                    pcm = self.arrivals.receive(data, msg.get('arrival_ns'))
                    if pcm is not None:
                        self.broadcast(b'\x02' + pcm, 2)
                except (ValueError, TypeError):
                    pass
            if kind in ('link', 'ble_write', 'bridge_state'):
                self.backend(msg)
        elif kind == 'button' and msg.get('key') in ('up', 'down', 'ok') and msg.get('event') in ('press', 'release'):
            key = msg['key']
            with self.lock:
                owner = self.key_owners.get(key)
                if msg['event'] == 'press':
                    if owner is not None and owner is not peer:
                        return
                    self.key_owners[key] = peer
                else:
                    if owner is not peer:
                        return
                    del self.key_owners[key]
            self.backend(msg)
        elif kind == 'mic':
            value = msg.get('hex', '')
            if isinstance(value, str) and len(value) == 320:
                try:
                    bytes.fromhex(value)
                except ValueError:
                    return
                self.backend(msg)

    def attach(self, peer):
        with self.lock:
            self.peers.add(peer)
            if peer.bridge:
                if self.bridge:
                    self.bridge.close()
                self.bridge = peer
                cached = list(self.cached.values())
            else:
                cached = [self.bridge_state]
                frame = self.frame
        for item in cached:
            peer.send(item)
        if not peer.bridge and frame:
            peer.send(b'\x01' + frame, 2)

    def detach(self, peer):
        with self.lock:
            self.peers.discard(peer)
            was_bridge = self.bridge is peer
            if was_bridge:
                self.bridge = None
                self.native_metrics = {}
        if was_bridge:
            self.broadcast({'type':'audio_boundary','source':'all'})
            self.backend({'type': 'link', 'connected': False, 'subscribed': False, 'central_id': '', 'max_value': 20})
            self.bridge_state = {'type': 'bridge_state', 'state': 'disconnected'}
            self.broadcast(self.bridge_state)
        elif not peer.bridge:
            with self.lock:
                keys = [k for k, owner in self.key_owners.items() if owner is peer]
                for key in keys:
                    del self.key_owners[key]
            for key in keys:
                self.backend({'type': 'button', 'key': key, 'event': 'release'})

    def snapshot(self):
        with self.lock:
            return {'type': 'diagnostics', 'uptime_s': round(time.monotonic() - self.started, 1),
                    'backend_available': self.backend_alive, 'bridge': self.bridge_state, 'native': self.native_metrics, 'backend': self.last_metrics, 'arrival': self.arrivals.snapshot(),
                    'web_queue_drops': sum(p.dropped for p in self.peers if not p.bridge), 'events': list(self.events)[-30:]}

    def metrics_loop(self):
        while self.running:
            time.sleep(.5)
            self.broadcast(self.snapshot())

    def close(self):
        self.running = False
        self.backend({'type': 'shutdown'})
        if self.process:
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.terminate()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, fmt, *args):
        if self.path not in ('/screen.png',):
            super().log_message(fmt, *args)

    def do_GET(self):
        self.path = self.path.split('?',1)[0]
        sim = self.server.simulator
        origin = self.headers.get('Origin')
        allowed = ('http://127.0.0.1:8765', 'http://localhost:8765')
        host = self.headers.get('Host', '')
        if host not in ('127.0.0.1:8765', 'localhost:8765') or origin and origin not in allowed:
            self.send_error(403); return
        if self.path in ('/ws', '/bridge') and self.headers.get('Upgrade', '').lower() == 'websocket':
            if self.path == '/ws' and origin not in allowed:
                self.send_error(403); return
            if self.path == '/bridge' and not sim.backend_alive:
                self.send_error(503,'Simulator backend not ready'); return
            key = self.headers.get('Sec-WebSocket-Key', '')
            try:
                if len(base64.b64decode(key, validate=True)) != 16:
                    raise ValueError
            except ValueError:
                self.send_error(400); return
            self.send_response(101)
            self.send_header('Upgrade', 'websocket'); self.send_header('Connection', 'Upgrade')
            self.send_header('Sec-WebSocket-Accept', base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode())
            self.end_headers()
            peer = Peer(self.connection, self.path == '/bridge')
            sim.attach(peer)
            try:
                while peer.alive:
                    opcode, payload = read_ws(self.rfile)
                    if opcode == 8:
                        break
                    if opcode == 9:
                        peer.send(payload, 10)
                    elif opcode == 1:
                        try:
                            sim.incoming(peer, json.loads(payload))
                        except (ValueError, TypeError):
                            pass
            except (EOFError, OSError, ValueError):
                pass
            finally:
                sim.detach(peer); peer.close(); self.close_connection = True
            return
        if self.path == '/':
            data, mime = (HERE/'index.html').read_bytes(), 'text/html; charset=utf-8'
        elif self.path == '/audio-worklet.js':
            data, mime = (HERE/'audio-worklet.js').read_bytes(), 'application/javascript'
        elif self.path == '/screen.png':
            if not sim.frame:
                self.send_error(503); return
            data, mime = rgb565_png(sim.frame), 'image/png'
        elif self.path == '/diagnostics.json':
            data, mime = json.dumps(sim.snapshot(), ensure_ascii=False, indent=2).encode(), 'application/json; charset=utf-8'
        else:
            self.send_error(404); return
        self.send_response(200); self.send_header('Content-Type', mime); self.send_header('Content-Length', str(len(data))); self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff'); self.end_headers(); self.wfile.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-backend', action='store_true', help='HTTP/WS smoke test only')
    args = parser.parse_args()
    sim = Simulator(not args.no_backend)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 8765), Handler)
    server.simulator = sim
    print('RF Link Web Passport: http://127.0.0.1:8765', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close(); sim.close()

if __name__ == '__main__':
    main()
