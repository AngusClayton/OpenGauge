#!/usr/bin/env python3
"""Build/run the real C++ gauge renderer and convert its RGB565 output to PNG."""
import argparse, struct, subprocess, sys, tempfile, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / 'tools' / 'host'
EXE = HOST / 'build' / 'gauge_preview.exe'
SOURCES = [ROOT/'src/GaugeRenderer.cpp', ROOT/'src/Display/GUI_Paint.cpp',
           ROOT/'src/Display/Font_nokia.cpp', HOST/'preview_main.cpp']

def build():
    inputs = SOURCES + list((HOST/'include').glob('*'))
    if EXE.exists() and EXE.stat().st_mtime >= max(p.stat().st_mtime for p in inputs): return
    EXE.parent.mkdir(parents=True, exist_ok=True)
    cmd = ['g++','-std=c++17','-O2','-o',str(EXE), *(str(x) for x in SOURCES),
           '-I'+str(HOST/'include'), '-I'+str(ROOT/'include'), '-I'+str(ROOT/'src')]
    print('Building host renderer...', file=sys.stderr)
    subprocess.run(cmd, check=True)

def png(raw_path, output, scale):
    raw = raw_path.read_bytes()
    if len(raw) != 240*240*2: raise RuntimeError('renderer returned an invalid framebuffer')
    rows = bytearray()
    for y in range(240):
        row=bytearray()
        for x in range(240):
            # GUI_Paint stores bytes in the order sent over SPI (RGB565 MSB first).
            c=struct.unpack_from('>H',raw,2*(y*240+x))[0]
            rgb=((c>>11&31)*255//31,(c>>5&63)*255//63,(c&31)*255//31)
            row.extend(bytes(rgb)*scale)
        for _ in range(scale): rows.append(0); rows.extend(row)
    def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    w=h=240*scale
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b'')
    output.parent.mkdir(parents=True,exist_ok=True); output.write_bytes(data)

def main():
    p=argparse.ArgumentParser(description='Preview using the real 32GUAGE C++ renderer')
    p.add_argument('--type',choices=('standard','shiftlight','gmeter'),default='standard')
    p.add_argument('--value',action='append',default=[],metavar='NAME=NUMBER')
    p.add_argument('--main-source',default='value'); p.add_argument('--min',type=float,default=0); p.add_argument('--max',type=float,default=100)
    p.add_argument('--unit',default=''); p.add_argument('--boost-units',action='store_true')
    p.add_argument('--secondary',action='append',default=[],metavar='SOURCE,PREFIX,SUFFIX,Y[,dynamic]')
    p.add_argument('--offline',action='store_true',help='simulate an offline IMU'); p.add_argument('-o','--output',type=Path,default=Path('preview.png'))
    p.add_argument('--scale',type=int,default=1)
    a=p.parse_args(); build()
    with tempfile.TemporaryDirectory() as d:
        raw=Path(d)/'frame.rgb565'
        cmd=[str(EXE),'--type',a.type,'--raw',str(raw),'--main-source',a.main_source,'--min',str(a.min),'--max',str(a.max),'--unit',a.unit]
        for v in a.value: cmd += ['--value',v]
        for s in a.secondary: cmd += ['--secondary',s]
        if a.boost_units: cmd += ['--boost-units']
        if a.offline: cmd += ['--offline']
        subprocess.run(cmd,check=True); png(raw,a.output,max(1,a.scale))
    print(f'Rendered real C++ {a.type} gauge to {a.output}')
if __name__=='__main__': main()
