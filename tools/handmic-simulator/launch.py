#!/usr/bin/env python3
"""Build and launch Web Passport without flashing or accessing the physical Passport."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--no-bridge', action='store_true')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    if not args.skip_build:
        subprocess.run([str(ROOT/'tools/dev'), 'simulator-build'], cwd=ROOT, check=True)
        if not args.no_bridge:
            subprocess.run([sys.executable,str(HERE/'build_bridge.py')], cwd=ROOT, check=True)
    server = subprocess.Popen([sys.executable,str(HERE/'server.py')], cwd=ROOT)
    try:
        ready = False
        for _ in range(150):
            if server.poll() is not None:
                raise RuntimeError('Web server failed (is port 8765 already in use?)')
            try:
                with urllib.request.urlopen('http://127.0.0.1:8765/diagnostics.json',timeout=.5) as response:
                    ready=response.status==200 and bool(json.load(response).get("backend"))
                if ready:
                    break
            except OSError:
                pass
            time.sleep(.1)
        if not ready:
            raise RuntimeError('Web server did not become ready')
        if not args.no_bridge:
            subprocess.run(['open','-a',str(ROOT/'artifacts/handmic-simulator/RFLinkBLESimulator.app')],check=True)
        if not args.no_browser:
            subprocess.run(['open','http://127.0.0.1:8765'],check=True)
        print('Web Passport is running. Ctrl-C stops the Web/backend; quit the RF Link BLE menu-bar app separately.',flush=True)
        server.wait()
    except KeyboardInterrupt:
        server.send_signal(2)
        server.wait(timeout=5)
    finally:
        if server.poll() is None:
            server.send_signal(2)
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.terminate()

if __name__=='__main__':
    main()
