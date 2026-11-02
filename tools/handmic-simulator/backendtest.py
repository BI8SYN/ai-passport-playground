#!/usr/bin/env python3
"""Black-box tests of the real core/LVGL simulator; run inside SDK container."""
import json
import adpcm
import os
from pathlib import Path
import queue
import struct
import subprocess
import tempfile
import threading
import time
import unittest

BINARY = os.environ.get('PASSPORT_SIMULATOR', '/build/handmic-simulator/backend/passport_simulator')


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.settings = Path(self.temp.name) / 'settings.json'
        self.start()

    def start(self):
        self.p = subprocess.Popen([BINARY, str(self.settings)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        self.events = queue.Queue()
        self.seen = []
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        self.request_id = self.transfer_id = 0
        self.sid = 0
        self.parts = {}

    def read(self):
        for line in self.p.stdout:
            try:
                self.events.put(json.loads(line))
            except Exception as exc:
                self.events.put({'type': 'invalid_output', 'message': str(exc)})

    def stop(self):
        if self.p.poll() is None:
            self.send({'type': 'shutdown'})
            self.p.wait(timeout=4)
        self.reader.join(timeout=1)
        self.assertEqual(self.p.returncode, 0, self.p.stderr.read())
        self.p.stdin.close()
        self.p.stdout.close()
        self.p.stderr.close()

    def tearDown(self):
        self.stop()
        self.temp.cleanup()

    def send(self, obj):
        self.p.stdin.write(json.dumps(obj, separators=(',', ':')) + '\n')
        self.p.stdin.flush()

    def wait(self, predicate, timeout=3):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            try:
                item = self.events.get(timeout=max(.01, until-time.monotonic()))
            except queue.Empty:
                break
            self.assertNotEqual(item['type'], 'invalid_output')
            self.seen.append(item)
            if item['type'] == 'ble_notify' and item['channel'] == 'control':
                raw = bytes.fromhex(item['hex'])
                transfer, offset, total = struct.unpack('<IHH', raw[:8])
                buf = self.parts.setdefault(transfer, bytearray())
                self.assertEqual(offset, len(buf))
                buf.extend(raw[8:])
                if len(buf) == total:
                    reply = json.loads(buf)
                    item = {'type': 'reply', 'reply': reply}
                    self.seen.append(item)
                    del self.parts[transfer]
            if predicate(item):
                return item
        self.fail('event timeout; recent=' + repr(self.seen[-6:])[:1800])

    def click(self, key, seconds=.025):
        self.send({'type': 'button', 'key': key, 'event': 'press'})
        time.sleep(seconds)
        self.send({'type': 'button', 'key': key, 'event': 'release'})

    def link(self, central='test-central'):
        self.send({'type': 'link', 'connected': True, 'subscribed': True,
                   'max_value': 244, 'central_id': central})

    def command(self, op, params, sid=None):
        self.request_id += 1
        self.transfer_id += 1
        request = {'v': 1, 'id': self.request_id, 'sid': self.sid if sid is None else sid,
                   'op': op, 'p': params}
        raw = json.dumps(request, separators=(',', ':')).encode()
        for offset in range(0, len(raw), 12):
            self.send({'type': 'ble_write', 'channel': 'control',
                       'hex': (struct.pack('<IHH', self.transfer_id, offset, len(raw))+raw[offset:offset+12]).hex()})
        reply = self.wait(lambda e: e['type'] == 'reply' and e['reply'].get('id') == self.request_id)['reply']
        self.assertTrue(reply['ok'], reply)
        return reply

    def connect(self, audio_format=2):
        self.click('ok')
        self.wait(lambda e: e['type'] == 'advertise' and e['enabled'])
        self.link()
        reply = self.command('hello', {'major': 1, 'minor': 1 if audio_format==2 else 0, 'format': audio_format, 'write_max': 244}, sid=0)
        self.sid = reply['sid']
        self.assertGreater(self.sid, 0)
        self.command('radio.update', {'context_id': 1, 'network': 'online', 'callsign': 'BI8SYN',
                                      'server': 'SIM', 'phase': 'idle', 'muted': False, 'tx_allowed': True})

    def flow(self, stream=0, next_seq=0, end=0):
        self.send({'type': 'ble_write', 'channel': 'app_flow',
                   'hex': struct.pack('<BBBBIIIII', 1, 0, 0, 0, self.sid, stream, next_seq, end, 0).hex()})

    def test_adpcm_speaker_and_mic(self):
        self.connect(audio_format=2)
        self.command('talker.update', {'context_id': 1, 'turn_id': 1, 'uid': 11, 'callsign': 'BH8ABC', 'active': True})
        self.command('stream.start', {'stream_id': 1, 'direction': 'speaker', 'press_id': 0, 'turn_id': 1})
        pcm = struct.pack('<160h', *([1000]*160))
        block = struct.pack('<hBB', 1000, 0, 0) + bytes(80)
        for seq in range(3):
            raw = struct.pack('<BBBBIII', 1, seq==0, 2, 160, self.sid, 1, seq) + block
            self.send({'type': 'ble_write', 'channel': 'speaker', 'hex': raw.hex()})
            time.sleep(.02)
        self.wait(lambda e: e['type']=='playback' and e['hex']==pcm.hex())
        self.command('stream.stop', {'stream_id': 1, 'reason': 'remote_end'})
        self.send({'type': 'button', 'key': 'ok', 'event': 'press'})
        press = self.wait(lambda e: e['type']=='reply' and e['reply'].get('event')=='ptt')['reply']['p']['press_id']
        self.command('stream.start', {'stream_id': 2, 'direction': 'mic', 'press_id': press, 'turn_id': 0})
        self.flow(2, 0, 32)
        for _ in range(2):
            self.send({'type': 'mic', 'hex': pcm[:160].hex()})
            time.sleep(.01)
        mic = self.wait(lambda e: e['type']=='ble_notify' and e.get('channel')=='mic')
        packet = bytes.fromhex(mic['hex'])
        self.assertEqual(len(packet), 100)
        self.assertEqual(packet[:4], bytes([1,1,2,160]))
        self.assertEqual(packet[16:], block)
        self.send({'type': 'button', 'key': 'ok', 'event': 'release'})

    def test_real_frame_bad_input_pair_binding_and_reconnect(self):
        frame = self.wait(lambda e: e['type'] == 'frame')
        self.assertEqual((frame['width'], frame['height']), (240, 320))
        pixels = bytes.fromhex(frame['hex'])
        self.assertEqual(len(pixels), 153600)
        self.assertGreater(len(set(struct.unpack('<76800H', pixels))), 5)
        self.p.stdin.write('{broken\n[]\n{"type":"mic","hex":"zz"}\n')
        self.p.stdin.flush()
        self.connect()
        self.send({'type': 'ble_write', 'channel': 'control',
                   'hex': (struct.pack('<IHH', 999, 0, 6) + b'broken').hex()})
        invalid = self.wait(lambda e: e['type'] == 'reply' and e['reply'].get('id') == 0)['reply']
        self.assertEqual(invalid['error'], 'bad_message')
        metrics = self.wait(lambda e: e['type'] == 'metrics' and e.get('sid') == self.sid)
        self.assertGreaterEqual(metrics['input_errors'], 4)
        self.assertTrue(metrics['virtual_bonded'])
        self.assertEqual(json.loads(self.settings.read_text())['virtualcentral'], 'test-central')
        self.assertEqual(self.settings.stat().st_mode & 0o777, 0o600)
        self.stop()
        self.start()
        self.wait(lambda e: e['type'] == 'advertise' and e['enabled'])
        self.link()
        reply = self.command('hello', {'major': 1, 'minor': 1, 'format': 2, 'write_max': 244}, sid=0)
        self.assertGreater(reply['sid'], 0)

    def test_speaker_clock_core_history_duplicate_and_timeout(self):
        self.connect()
        self.command('talker.update', {'context_id': 1, 'turn_id': 1, 'uid': 11, 'callsign': 'BH8ABC', 'active': True})
        self.command('stream.start', {'stream_id': 1, 'direction': 'speaker', 'press_id': 0, 'turn_id': 1})
        pcm = struct.pack('<160h', *([12000] * 160))
        raw = struct.pack('<BBBBIII', 1, 1, 2, 160, self.sid, 1, 0) + adpcm.encode(pcm)
        for _ in range(2):
            self.send({'type': 'ble_write', 'channel': 'speaker', 'hex': raw.hex()})
        self.wait(lambda e: e['type'] == 'playback' and e['hex'] == pcm.hex())
        self.wait(lambda e: e['type'] == 'playback' and e['hex'] == '00'*320)
        metrics = self.wait(lambda e: e['type'] == 'metrics' and e.get('audio_accepted', 0) >= 1)
        self.assertEqual(metrics['audio_accepted'], 1)
        self.assertGreaterEqual(metrics['duplicate'], 1)
        self.assertGreater(metrics['underrun'], 0)
        self.assertEqual(metrics['history'][0]['callsign'], 'BH8ABC')
        self.assertEqual(metrics['direction'], 'none')  # 300ms real core audio lease

    def test_mic_requires_ptt_credit_and_real_microphone_input(self):
        self.connect()
        self.send({'type': 'button', 'key': 'ok', 'event': 'press'})
        ptt = self.wait(lambda e: e['type'] == 'reply' and e['reply'].get('event') == 'ptt')['reply']
        press = ptt['p']['press_id']
        self.command('stream.start', {'stream_id': 1, 'direction': 'mic', 'press_id': press, 'turn_id': 0})
        self.flow(1, 0, 32)
        pcm = struct.pack('<160h', *range(160))
        self.send({'type': 'mic', 'hex': pcm[:160].hex()})
        self.send({'type': 'mic', 'hex': pcm[160:].hex()})
        wire = self.wait(lambda e: e['type'] == 'ble_notify' and e['channel'] == 'mic')
        self.assertEqual(bytes.fromhex(wire['hex'])[16:], adpcm.encode(pcm))
        self.send({'type': 'button', 'key': 'ok', 'event': 'release'})
        self.wait(lambda e: e['type'] == 'reply' and e['reply'].get('event') == 'ptt' and not e['reply']['p']['down'])
        self.send({'type': 'mic', 'hex': pcm[:160].hex()})
        metrics = self.wait(lambda e: e['type'] == 'metrics')
        self.assertEqual(metrics['tx_packets'], 1)
        self.assertEqual(metrics['direction'], 'none')

    def test_spectrum_setting_defaults_off_and_survives_restart(self):
        self.connect()
        self.wait(lambda e: e['type'] == 'metrics' and e.get('spectrum_enabled') is False)
        self.click('up', .55)
        for _ in range(5):
            self.click('down')
        self.click('ok')
        self.wait(lambda e: e['type'] == 'metrics' and e.get('spectrum_enabled') is True)
        self.assertTrue(json.loads(self.settings.read_text())['spectrum'])
        self.stop()
        self.start()
        self.wait(lambda e: e['type'] == 'metrics' and e.get('spectrum_enabled') is True)
        self.click('up', .55)
        for _ in range(5):
            self.click('down')
        self.click('ok')
        self.wait(lambda e: e['type'] == 'metrics' and e.get('spectrum_enabled') is False)
        self.assertFalse(json.loads(self.settings.read_text())['spectrum'])

    def test_keys_settings_theme_and_virtual_unpair(self):
        self.connect()
        self.click('up')  # quick volume +5, settled by product tick
        self.click('up', .55)  # real long press opens settings
        for _ in range(4):
            self.click('down')
        self.click('ok')  # theme page
        self.click('down')
        self.click('ok')  # persist amber
        self.wait(lambda e: e['type'] == 'metrics' and e.get('theme') == 'amber')
        saved = json.loads(self.settings.read_text())
        self.assertEqual((saved['volume'], saved['theme']), (95, 'amber'))
        self.click('ok', .85)  # 800ms back to main
        self.click('up', .55)
        for _ in range(3):
            self.click('down')  # phone menu
        self.click('ok')
        self.click('down')  # remove saved phone
        self.click('ok')
        self.click('up')  # confirm yes
        self.click('ok')
        self.wait(lambda e: e['type'] == 'reset_link')
        metrics = self.wait(lambda e: e['type'] == 'metrics' and not e.get('virtual_bonded', True))
        self.assertFalse(metrics['connected'])
        self.assertEqual(json.loads(self.settings.read_text())['virtualcentral'], '')

    def test_unauthorized_link_subscription_loss_and_clock(self):
        self.link('untrusted')
        self.wait(lambda e: e['type'] == 'reset_link')
        self.connect()
        self.send({'type': 'link', 'connected': True, 'central_id': 'test-central',
                   'subscribed': False, 'max_value': 244})
        self.wait(lambda e: e['type'] == 'characteristic' and e['channel'] == 'status'
                  and not json.loads(e['text'])['sid'])
        self.link()
        reply = self.command('hello', {'major': 1, 'minor': 1, 'format': 2, 'write_max': 244}, sid=0)
        self.assertGreater(reply['sid'], self.sid)
        first = self.wait(lambda e: e['type'] == 'metrics' and e.get('sid') == reply['sid'])
        # The input queue must keep advancing real time while malformed messages arrive.
        self.p.stdin.write('{}\n' * 3000)
        self.p.stdin.flush()
        second = self.wait(lambda e: e['type'] == 'metrics' and e.get('input_errors', 0) >= 3000)
        dt = second['uptime_ms'] - first['uptime_ms']
        frames = second['playback_frames'] - first['playback_frames']
        self.assertGreater(dt, 900)
        self.assertGreater(frames, dt / 20 * .8)
        self.assertLessEqual(frames, dt / 20 * 1.1)

    def test_pair_cancel_closes_uncommitted_link(self):
        self.click('ok')
        self.wait(lambda e: e['type'] == 'advertise' and e['enabled'])
        self.link()
        self.click('down')
        self.wait(lambda e: e['type'] == 'reset_link')
        self.assertFalse(self.settings.exists())

    def test_arrival_jitter_excludes_inter_turn_silence_and_stale_stream(self):
        self.connect()
        pcm = bytes(320)
        arrival = time.monotonic_ns()

        def speaker(stream, seq, timestamp):
            raw = struct.pack('<BBBBIII', 1, int(seq == 0), 2, 160, self.sid, stream, seq) + adpcm.encode(pcm)
            self.send({'type': 'ble_write', 'channel': 'speaker', 'hex': raw.hex(),
                       'arrival_ns': timestamp})

        self.command('talker.update', {'context_id': 1, 'turn_id': 1, 'uid': 11,
                                        'callsign': 'BH8ABC', 'active': True})
        self.command('stream.start', {'stream_id': 1, 'direction': 'speaker', 'press_id': 0, 'turn_id': 1})
        speaker(1, 0, arrival)
        speaker(1, 1, arrival + 20_000_000)
        self.command('stream.stop', {'stream_id': 1, 'reason': 'remote_end'})
        time.sleep(.55)
        self.command('talker.update', {'context_id': 1, 'turn_id': 2, 'uid': 22,
                                        'callsign': 'BG8XYZ', 'active': True})
        self.command('stream.start', {'stream_id': 2, 'direction': 'speaker', 'press_id': 0, 'turn_id': 2})
        speaker(2, 0, arrival + 600_000_000)
        speaker(1, 2, arrival + 605_000_000)  # stale ATT packet cannot alter new-stream jitter
        speaker(2, 1, arrival + 620_000_000)
        metrics = self.wait(lambda e: e['type'] == 'metrics' and e.get('audio_accepted', 0) >= 4)
        self.assertEqual(metrics['rx_interval_scope'], 'latest_speaker_stream')
        self.assertEqual((metrics['rx_arrival_sid'], metrics['rx_arrival_stream_id']), (self.sid, 2))
        self.assertEqual(metrics['rx_arrival_intervals'], 1)
        self.assertAlmostEqual(metrics['rx_interval_mean_ms'], 20)
        self.assertAlmostEqual(metrics['rx_interval_max_ms'], 20)
        self.assertAlmostEqual(metrics['rx_jitter_stddev_ms'], 0)
        reply = self.command('hello', {'major': 1, 'minor': 1, 'format': 2, 'write_max': 244}, sid=0)
        metrics = self.wait(lambda e: e['type'] == 'metrics' and e.get('sid') == reply['sid'])
        self.assertEqual(metrics['rx_arrival_sid'], reply['sid'])
        self.assertEqual(metrics['rx_arrival_stream_id'], 0)
        self.assertEqual(metrics['rx_arrival_intervals'], 0)

    def test_processed_audio_boundary_precedes_stream_playback_and_stop_silence(self):
        self.connect()
        self.command('talker.update', {'context_id': 1, 'turn_id': 1, 'uid': 11,
                                        'callsign': 'BH8ABC', 'active': True})
        self.command('stream.start', {'stream_id': 1, 'direction': 'speaker', 'press_id': 0, 'turn_id': 1})
        first = self.wait(lambda e: e['type'] in ('audio_boundary', 'playback'))
        self.assertEqual(first['type'], 'audio_boundary')
        self.assertEqual((first['source'], first['sid'], first['stream_id'], first['direction']),
                         ('processed', self.sid, 1, 'speaker'))
        pcm = struct.pack('<160h', *([12000] * 160))
        wire = struct.pack('<BBBBIII', 1, 1, 2, 160, self.sid, 1, 0) + adpcm.encode(pcm)
        self.send({'type': 'ble_write', 'channel': 'speaker', 'hex': wire.hex()})
        self.wait(lambda e: e['type'] == 'playback' and e['hex'] == pcm.hex())
        self.command('stream.stop', {'stream_id': 1, 'reason': 'remote_end'})
        first = self.wait(lambda e: e['type'] in ('audio_boundary', 'playback'))
        self.assertEqual(first['type'], 'audio_boundary')
        self.assertEqual((first['source'], first['sid'], first['stream_id'], first['direction']),
                         ('processed', self.sid, 0, 'none'))
        silence = self.wait(lambda e: e['type'] in ('audio_boundary', 'playback'))
        self.assertEqual(silence['type'], 'playback')
        self.assertEqual(silence['hex'], '00' * 320)


if __name__ == '__main__':
    unittest.main(verbosity=2)
