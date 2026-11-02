from pathlib import Path
import importlib.util
import io
import struct
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('handmic_simulator_server',ROOT/'tools/handmic-simulator/server.py')
server=importlib.util.module_from_spec(spec);spec.loader.exec_module(server)
class SimulatorWebTests(unittest.TestCase):
    def test_adpcm_arrival_decodes_and_rejects_bad_blocks(self):
        m=server.ArrivalMetrics()
        block=struct.pack('<hBB',1000,0,0)+bytes(80)
        packet=bytes([1,1,2,160])+struct.pack('<III',3,1,0)+block
        self.assertEqual(m.receive(packet),struct.pack('<160h',*([1000]*160)))
        self.assertEqual(m.snapshot()['rms'],1000)
        bad=bytearray(packet);bad[18]=89
        self.assertIsNone(m.receive(bytes(bad)))
        self.assertEqual(m.snapshot()['invalid'],1)
        legacy=bytes([1,1,1,80])+struct.pack('<III',3,1,0)+bytes(160)
        self.assertIsNone(m.receive(legacy))
        opus=bytes([1,1,3,160])+struct.pack('<III',3,1,0)+bytes(40)
        self.assertIsNone(m.receive(opus))
        self.assertEqual(m.snapshot()['invalid'],3)

    def test_adpcm_arrival_tracks_wire_sequence_and_streams(self):
        m=server.ArrivalMetrics()
        def packet(seq,stream=1,flags=0):
            return bytes([1,flags,2,160])+struct.pack('<III',3,stream,seq)+struct.pack('<hBB',1000,0,0)+bytes(80)
        self.assertEqual(len(m.receive(packet(0,flags=1),1000000)),320)
        m.receive(packet(2),11000000);m.receive(packet(2),12000000);m.receive(packet(0,2),13000000)
        s=m.snapshot();self.assertEqual(s['sequence_missing'],1);self.assertEqual(s['duplicates'],1);self.assertEqual(s['rms'],1000)
        self.assertEqual(s['discontinuities'],1);self.assertIsNone(m.receive(b'bad'));self.assertEqual(m.snapshot()['invalid'],1)
    def test_websocket_mask_lengths_and_limits(self):
        for size in (0,7,126,65536):
            value=b'x'*size;mask=b'abcd';head=server.ws_frame(value);masked=bytes([head[0],head[1]|128])+head[2:len(head)-size if size else len(head)]+mask+bytes(c^mask[i%4] for i,c in enumerate(value))
            opcode,result=server.read_ws(io.BytesIO(masked));self.assertEqual((opcode,result),(1,value))
        with self.assertRaises(ValueError):server.read_ws(io.BytesIO(b'\x81\x01x'))
    def test_frame_rgb565_to_png(self):
        png=server.rgb565_png(bytes(153600));self.assertTrue(png.startswith(b'\x89PNG'))
        with self.assertRaises(ValueError):server.rgb565_png(b'bad')

    def test_backend_failure_revokes_bridge_and_clears_status(self):
        sim=server.Simulator(False)
        class Peer:
            bridge=True
            alive=True
            def send(self,m,opcode=1):pass
            def close(self):self.alive=False
        peer=Peer();sim.attach(peer)
        sim.native_metrics={'central_id':'old','subscriptions':[1,2,3]}
        sim.last_metrics={'connected':True,'ready':True,'rx_packets':100}
        sim.backend_alive=True;sim.backend_failed()
        d=sim.snapshot();self.assertFalse(peer.alive);self.assertFalse(d['backend_available'])
        self.assertFalse(d['backend']['connected']);self.assertEqual(d['native'],{});self.assertEqual(d['backend']['rx_packets'],100)
        sim.close()
    def test_native_disconnect_clears_old_subscription_snapshot(self):
        sim=server.Simulator(False)
        class Peer:
            bridge=True;alive=True
            def send(self,m,opcode=1):pass
            def close(self):self.alive=False
        peer=Peer();sim.attach(peer);sim.native_metrics={'central_id':'old','subscriptions':[1,2,3]}
        sim.detach(peer);self.assertEqual(sim.snapshot()['native'],{});sim.close()

    def test_dead_backend_cannot_revive_from_buffered_stdout(self):
        sim=server.Simulator(False);sim.backend_alive=True
        sim.backend_failed();sim.output({'type':'metrics','connected':True,'ready':True,'sid':99})
        sim.output({'type':'characteristic','channel':'status','text':'{}'})
        self.assertFalse(sim.snapshot()['backend_available']);self.assertFalse(sim.snapshot()['backend']['connected'])
        self.assertEqual(sim.cached,{})
        sim.close()
