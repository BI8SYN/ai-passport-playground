#!/usr/bin/env python3
"""Scoped, explicitly authorized RF Link handmic device operations through Docker only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]

def run(args):
    print('+ '+' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),check=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['inspect','flash','monitor'])
    parser.add_argument('--port',required=True)
    parser.add_argument('--archive',required=True,type=Path)
    parser.add_argument('--container',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    archive=args.archive.resolve()
    archive.relative_to(ROOT/'artifacts/rf-link-handmic')
    if not args.container:
        if not args.port.startswith('/dev/cu.usbmodem') or not Path(args.port).exists():
            raise ValueError('Select the discovered Passport USB serial port')
        output=ROOT/'artifacts/rf-link-handmic-validation'
        output.mkdir(parents=True,exist_ok=True)
        run(['docker','run','--rm','--device',args.port+':/dev/passport',
             '--mount',f'type=bind,src={ROOT},dst=/workspace,readonly',
             '--mount',f'type=bind,src={output},dst=/results',
             'ai-passport-sdk:idf5.5.3','python3','/workspace/tools/handmic_device.py',
             args.action,'--container','--port','/dev/passport','--archive','/workspace/'+str(archive.relative_to(ROOT))])
        return
    if not Path('/.dockerenv').exists():
        raise ValueError('Container mode requires Docker')
    sys.path.insert(0,str(ROOT/'tools'))
    from package_firmware import verify
    manifest=verify(archive,'rf-link-handmic')
    if manifest['image_offsets']['rf-link-handmic.bin'] != 0x10000:
        raise ValueError('This scoped device tool only supports the baseline 0x10000 app offset')
    import esptool
    major=int(esptool.__version__.split('.')[0])
    def command(name,*tail,after='no-reset'):
        # Command spellings changed in esptool 5; both forms are explicit.
        if major<5:name=name.replace('-','_');after=after.replace('-','_')
        run([sys.executable,'-m','esptool','--chip','esp32c3','--port',args.port,
             '--before','usb-reset' if major>=5 else 'usb_reset','--after',after,name,*tail])
    if args.action in ('inspect','flash'):
        partition=Path('/results/device-partition.bin')
        command('read-flash','0x8000','0x1000',partition)
        expected=(archive/'partition_table/partition-table.bin').read_bytes()
        if partition.read_bytes()[:len(expected)]!=expected:
            raise ValueError('Device partition differs; STOP without writing')
        print('Device partition matches verified archive: PASS',flush=True)
        if args.action=='flash':
            command('write-flash','0x10000',archive/'rf-link-handmic.bin',after='hard-reset')
            command('verify-flash','0x10000',archive/'rf-link-handmic.bin',after='hard-reset')
            receipt={'archive':archive.name,'app_sha256':manifest['files']['rf-link-handmic.bin']['sha256'],
                     'offset':65536,'bytes':manifest['files']['rf-link-handmic.bin']['size'],
                     'partition_match':True,'write_and_verify':'PASS','at_unix':time.time(),
                     'backup':False,'efuse_write':False}
            Path('/results/flash-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    else:
        import serial
        # No intentional reset; bounded observation, then always release port.
        port=serial.Serial(port=None,baudrate=115200,timeout=.2)
        port.dtr=False;port.rts=False;port.port=args.port
        data=bytearray()
        try:
            port.open();deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                chunk=port.read(4096)
                if chunk:
                    data.extend(chunk);print(chunk.decode('utf-8',errors='replace'),end='',flush=True)
        finally:
            port.close()
            Path('/results/monitor-'+time.strftime('%Y%m%d-%H%M%S',time.gmtime())+'.log').write_bytes(data)

if __name__=='__main__':
    try:main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}',file=sys.stderr);sys.exit(1)
