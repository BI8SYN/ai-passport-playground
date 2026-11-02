#!/usr/bin/env python3
"""Exercise only the local virtual buttons and advertising; no physical-device access."""
import base64
import json
import os
import socket
import struct
import time
import urllib.request

class Client:
    def __init__(self):
        self.sock=socket.create_connection(('127.0.0.1',8765));self.stream=self.sock.makefile('rb')
        key=base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(('GET /ws HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nOrigin: http://127.0.0.1:8765\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: '+key+'\r\n\r\n').encode())
        assert b'101' in self.stream.readline()
        while self.stream.readline()!=b'\r\n':pass
    def send(self,msg):
        data=json.dumps(msg).encode();mask=os.urandom(4);n=len(data)
        head=bytes([0x81,0x80|n]) if n<126 else b'\x81\xfe'+struct.pack('!H',n)
        self.sock.sendall(head+mask+bytes(x^mask[i%4] for i,x in enumerate(data)))
    def key(self,key,duration=.08):
        self.send({'type':'button','key':key,'event':'press'});time.sleep(duration);self.send({'type':'button','key':key,'event':'release'});time.sleep(.12)
    def close(self):self.sock.close();self.stream.close()

def status():
    with urllib.request.urlopen('http://127.0.0.1:8765/diagnostics.json') as f:return json.load(f)

def main():
    c=Client();initial=status();assert not initial['native'].get('central_id'),'Do not run while iOS is connected'
    assert initial['backend'].get('page')==6,'Start at initial connection page'
    c.key('ok');time.sleep(1)
    d=status();assert d['backend']['pair_window'],d
    assert d['bridge']['state']=='advertising',d['bridge']
    print('Virtual OK→60sec window→actual Mac BLE advertising: PASS')
    c.key('down');c.key('up',.65)
    d=status();assert d['backend']['page']==1,d['backend']['page']
    # Settings menu index3 -> phone -> connect -> initial connection page.
    for _ in range(3):c.key('down')
    c.key('ok');c.key('ok');d=status();assert d['backend']['page']==6,d['backend']['page']
    c.close();print('Virtual menu/navigation and restore initial page: PASS')
if __name__=='__main__':main()
