#!/usr/bin/env python3
"""Build/run the real C++ gauge renderer and convert its RGB565 output to PNG."""
import argparse, json, struct, subprocess, sys, tempfile, zlib
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
            # Mark the square framebuffer area that is physically hidden by the
            # round LCD. This is preview-only and never alters production output.
            if (x - 119.5) ** 2 + (y - 119.5) ** 2 > 120 ** 2:
                rgb=(96,96,96)
            else:
                # GUI_Paint stores bytes in the order sent over SPI (RGB565 MSB first).
                c=struct.unpack_from('>H',raw,2*(y*240+x))[0]
                rgb=((c>>11&31)*255//31,(c>>5&63)*255//63,(c&31)*255//31)
            row.extend(bytes(rgb)*scale)
        for _ in range(scale): rows.append(0); rows.extend(row)
    def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    w=h=240*scale
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b'')
    output.parent.mkdir(parents=True,exist_ok=True); output.write_bytes(data)

def render_host(args, output, scale=1):
    """Run the native renderer and convert its RGB565 framebuffer to PNG."""
    build()
    with tempfile.TemporaryDirectory() as d:
        raw=Path(d)/'frame.rgb565'
        subprocess.run([str(EXE), *args, '--raw', str(raw)], check=True)
        png(raw, output, max(1, scale))

def render_config_preview(config, gauge_index, samples, output, scale=1):
    """Render one versioned config document through the production C++ code."""
    gauges=config['gauges']
    gauge=gauges[gauge_index]
    gauge_type=gauge['type']
    args=['--type', gauge_type]
    if gauge_type == 'standard':
        args += ['--main-source', gauge.get('mainSourceId','value'),
                 '--min', str(gauge.get('minVal',0)), '--max', str(gauge.get('maxVal',100)),
                 '--unit', gauge.get('unitLabel','')]
        if gauge.get('boostUnits', False): args += ['--boost-units']
        for secondary in gauge.get('secondaries',[]):
            fields=[secondary.get('sourceId',''), secondary.get('prefix',''), secondary.get('suffix','')]
            if secondary.get('rangeColors'):
                fields += ['range', str(secondary.get('lowerThreshold',0)), str(secondary.get('upperThreshold',100)),
                           secondary.get('colorBelow','blue'), secondary.get('colorBetween','cyan'), secondary.get('colorAbove','red')]
            args += ['--secondary', ','.join(fields)]
    elif gauge_type == 'shiftlight':
        targets=gauge.get('shiftTargets',[6500,6300,6100,6000,5800,0])
        args += ['--shift-targets', ','.join(str(int(value)) for value in targets)]
    elif gauge_type == 'accelTimer':
        args += ['--main-source', gauge.get('mainSourceId','speed'),
                 '--min', str(gauge.get('minVal',0)), '--max', str(gauge.get('maxVal',100)),
                 '--unit', gauge.get('unitLabel','km/h')]
    for name, value in samples.items():
        args += ['--value', f'{name}={value}']
    render_host(args, Path(output), scale)

def main():
    p=argparse.ArgumentParser(description='Preview using the real OpenGauge C++ renderer')
    p.add_argument('--type',choices=('standard','shiftlight','gmeter','accelTimer'),default='standard')
    p.add_argument('--config',type=Path,help='versioned config JSON document')
    p.add_argument('--gauge-index',type=int,default=0,help='zero-based gauge index for --config')
    p.add_argument('--samples',default='{}',help='JSON object of sample values for --config')
    p.add_argument('--value',action='append',default=[],metavar='NAME=NUMBER')
    p.add_argument('--main-source',default='value'); p.add_argument('--min',type=float,default=0); p.add_argument('--max',type=float,default=100)
    p.add_argument('--unit',default=''); p.add_argument('--boost-units',action='store_true')
    p.add_argument('--secondary',action='append',default=[],metavar='SOURCE,PREFIX,SUFFIX[,range,low,high,below,between,above]')
    p.add_argument('--offline',action='store_true',help='simulate an offline IMU'); p.add_argument('-o','--output',type=Path,default=Path('preview.png'))
    p.add_argument('--scale',type=int,default=1)
    a=p.parse_args()
    if a.config:
        config=json.loads(a.config.read_text(encoding='utf-8'))
        samples=json.loads(a.samples)
        render_config_preview(config, a.gauge_index, samples, a.output, a.scale)
        rendered_type=config['gauges'][a.gauge_index]['type']
    else:
        cmd=['--type',a.type,'--main-source',a.main_source,'--min',str(a.min),'--max',str(a.max),'--unit',a.unit]
        for v in a.value: cmd += ['--value',v]
        for s in a.secondary: cmd += ['--secondary',s]
        if a.boost_units: cmd += ['--boost-units']
        if a.offline: cmd += ['--offline']
        render_host(cmd, a.output, a.scale)
        rendered_type=a.type
    print(f'Rendered real C++ {rendered_type} gauge to {a.output}')
if __name__=='__main__': main()
