#!/usr/bin/env python3
"""Compare displayed pixels after disjoint damage with a full repaint.

Native screen capture is essential: QWidget.grab() would repaint the page and
hide the very errors this test checks. Run under xvfb-run.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix='wisp-paint-damage-') as temp:
    root = Path(temp)

    def run(name, page, actions, *, dpr=1, zoom=100, full=False):
        dest = root / name
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR=str(dpr),
                   QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', '640x480',
                '--capture-screen', '--capture-settle', '50', '--capture-timeout', '10000',
                f'--scale={zoom}']
        if full:
            args += ['--capture-no-scroll-blit']
        for kind, value in actions:
            args += ['--capture-' + kind, value]
        process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=20)
        assert process.returncode == 0, (name, process.returncode, process.stderr[-4000:])
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['load_complete'] and meta['result'] == 'complete', (name, meta)
        steps = json.loads((dest / 'steps.json').read_text())
        return (dest / 'page.png').read_bytes(), steps

    (root / 'child.html').write_text('''<!doctype html><style>
        body{margin:0;background:#dce}
        div{position:fixed;left:0;top:20px;width:400px;height:240px;background:rgba(10,90,200,.5)}
        </style><div>Fixed child extends beyond its frame</div>''')
    page = root / 'positioned.html'
    page.write_text('''<!doctype html><style>
        body{margin:0;background:rgba(230,210,140,.6);font:16px/24px sans-serif;height:1800px}
        .absolute{position:absolute;top:15px;left:10px;width:600px;height:400px;background:rgba(220,40,100,.45)}
        .fixed{position:fixed;top:30px;left:20px;width:580px;height:330px;background:rgba(30,140,200,.35)}
        .absolute:before{content:"Positioned pseudo";position:absolute;top:70px;left:50px;width:480px;height:180px;background:rgba(40,150,20,.4)}
        iframe{position:absolute;top:220px;left:90px;width:380px;height:120px;border:5px solid #753}
        </style><div class="absolute">Absolute</div><div class="fixed">Fixed</div>
        <iframe src="child.html"></iframe>''')

    # All positioned layers cross more than one dirty rectangle. A painter that
    # expands a pass to the viewport blends another rectangle more than once.
    damage = '0,0,200,120;300,20,300,130;40,200,180,150;300,250,270,180'
    for zoom, dpr in ((100, 1), (100, 2), (150, 1)):
        name = f'positioned-{zoom}-{dpr}'
        prefix = [('scroll', '0,60')]
        actual, steps = run(name, page, [*prefix, ('repaint', damage)], zoom=zoom, dpr=dpr)
        reference, _ = run(name + '-full', page, [*prefix, ('repaint', '0,0,640,480')], zoom=zoom, dpr=dpr)
        assert actual == reference, (name, 'disjoint repaint differs from full repaint')
        before, after = (step['widget_paint'] for step in steps[-2:])
        assert 0 < after['paint_pixels'] - before['paint_pixels'] < 640 * 480, (name, before, after)

    page = root / 'transformed.html'
    page.write_text('''<!doctype html><style>
        body{margin:0;background:white;height:1800px}
        div{position:absolute;left:60px;top:40px;width:470px;height:320px;
            background:rgba(40,90,210,.45);transform:rotate(25deg)}
        </style><div></div>''')
    for dpr in (1, 2):
        name = f'transformed-{dpr}'
        prefix = [('scroll', '0,30')]
        actual, steps = run(name, page, [*prefix, ('repaint', '20,20,220,340;260,20,220,340')], dpr=dpr)
        reference, _ = run(name + '-full', page, [*prefix, ('repaint', '0,0,640,480')], dpr=dpr)
        assert actual == reference, (name, 'transformed partial painting differs from full painting')
        before, after = (step['widget_paint'] for step in steps[-2:])
        assert after['paint_pixels'] - before['paint_pixels'] == 640 * 480, (name, before, after)
        assert after['scroll_blits'] == 0, (name, after)

    # Eligibility must follow style changes in both directions, including
    # transformed descendants inside independently laid-out child documents.
    page = root / 'restyle.html'
    page.write_text('''<!doctype html><style>
        body{margin:0;background:white;height:1800px}
        label{display:block;height:30px}input{display:none}
        div{width:470px;height:320px;background:rgba(40,90,210,.45)}
        :checked~div{transform:rotate(25deg)}
        </style><input type="checkbox" id="rotate"><label for="rotate">Rotate</label><div></div>''')
    prefix = []
    for transformed in (True, False):
        name = f'restyle-{transformed}'
        prefix += [('click-label', 'rotate')]
        actual, steps = run(name, page, [*prefix, ('repaint', damage)])
        reference, _ = run(name + '-full', page, [*prefix, ('repaint', '0,0,640,480')])
        assert actual == reference, name
        before, after = (step['widget_paint'] for step in steps[-2:])
        area = after['paint_pixels'] - before['paint_pixels']
        assert (area == 640 * 480) if transformed else (0 < area < 640 * 480), (name, before, after)

    page = root / 'transformed-frame.html'
    page.write_text('''<!doctype html><style>body{margin:0;background:white}
        iframe{width:600px;height:400px;border:0}</style><iframe src="transformed.html"></iframe>''')
    actual, steps = run('transformed-frame', page, [('repaint', damage)])
    reference, _ = run('transformed-frame-full', page, [('repaint', '0,0,640,480')])
    assert actual == reference, 'transformed frame partial painting differs from full painting'
    before, after = (step['widget_paint'] for step in steps[-2:])
    assert after['paint_pixels'] - before['paint_pixels'] == 640 * 480, (before, after)

    # Scrollbars lie just beyond the old viewport. Antialiased outline ink must
    # stay within their allocated rectangle, so the newly exposed strip suffices.
    page = root / 'scrollbars.html'
    page.write_text('''<!doctype html><style>
        body{margin:0;background:white;width:1800px;height:1800px}
        .port{position:absolute;width:180px;height:180px;overflow:auto}
        .inside{width:500px;height:500px}
        #bottom{top:480px;left:60px}#right{top:40px;left:640px}
        </style><div class="port" id="bottom"><div class="inside"></div></div>
        <div class="port" id="right"><div class="inside"></div></div>''')
    for dpr in (1, 2):
        for axis, positions in (('vertical', ['0,100']), ('horizontal', ['100,0']),
                                ('diagonal', ['100,100', '200,200', '100,100'])):
            name = f'scrollbar-{axis}-{dpr}'
            actions = [('scroll', pos) for pos in positions]
            actual, steps = run(name, page, actions, dpr=dpr)
            reference, _ = run(name + '-full', page, actions, dpr=dpr, full=True)
            assert actual == reference, (name, 'scrollbar edge differs after viewport copy')
            assert steps[-1]['widget_paint']['scroll_blits'] > 0, (name, steps[-1])

print('Qt paint damage: disjoint clips, alpha, frames, transform fallback, zoom, DPR and scrollbar seams passed.')
