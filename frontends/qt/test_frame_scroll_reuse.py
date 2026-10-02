#!/usr/bin/env python3
"""Native frame/lifecycle pixels must agree with disabling scroll reuse."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix='wisp-frame-reuse-') as directory:
    root = Path(directory)

    def run(name, page, actions, dpr, full):
        dest = root / name
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR=str(dpr),
                   QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', '800x600',
                '--capture-screen', '--capture-settle', '50', '--capture-timeout', '15000', '--core_select_menu=1']
        if full:
            args += ['--capture-no-scroll-blit']
        for kind, value in actions:
            args += ['--capture-' + kind, value]
        process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=25)
        assert process.returncode == 0 and 'runtime error:' not in process.stderr, (name, process.stderr[-4000:])
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['load_complete'] and meta['result'] == 'complete', (name, meta)
        return (dest / 'page.png').read_bytes(), json.loads((dest / 'steps.json').read_text())

    def compare(name, page, actions, dpr=1, last_blits=1, last_repaints=0):
        actual, steps = run(name, page, actions, dpr, False)
        reference, _ = run(name + '-full', page, actions, dpr, True)
        assert actual == reference, (name, 'copied pixels differ from full repaint')
        previous, last = (step['widget_paint'] for step in steps[-2:])
        assert last['scroll_blits'] - previous['scroll_blits'] == last_blits, (name, previous, last)
        assert last['scroll_repaints'] - previous['scroll_repaints'] == last_repaints, (name, previous, last)
        return steps

    (root / 'nested.html').write_text('''<!doctype html><style>
        body{margin:0;height:600px;background:#bcd}p{margin:0;height:25px}
        header{position:fixed;top:8px;left:15px;background:rgba(255,70,20,.5)}</style>
        <header>Nested fixed</header><p>Nested one</p><p>Nested two</p>''')
    (root / 'child.html').write_text('''<!doctype html><style>
        body{margin:0;height:800px;background:#eed;font:16px/24px sans-serif}
        header{position:fixed;top:15px;left:5px;background:rgba(40,170,70,.5)}
        iframe{display:block;width:150px;height:60px;border:3px solid #478}
        #change:checked~p{background:#f95}p{margin:0;height:40px}</style>
        <header>Child fixed</header><input id="change" type="checkbox"><label for="change">Change</label>
        <p>Child row one</p><iframe src="nested.html"></iframe><p>Child row two</p>''')
    (root / 'transformed.html').write_text('''<!doctype html><style>
        body{height:800px}p{transform:rotate(10deg);background:#6af}</style><p>Transformed child</p>''')
    common = '''<!doctype html><style>body{margin:0;height:3000px;font:16px/24px sans-serif;background:#f8f0df}
        .before{height:100px}iframe{display:block;width:240px;height:170px;border:4px solid #579}
        .holder{height:1100px;padding-left:20px}header{position:sticky;top:0;height:30px;background:rgba(30,100,190,.5)}
        p{margin:0}#toggle{position:absolute;left:350px;top:350px}
        label{position:absolute;left:380px;top:350px}</style>'''
    page = root / 'frames.html'
    page.write_text(common + '<div class="before"></div><div class="holder"><header>Parent sticky</header>'
                    '<iframe src="child.html"></iframe></div>')
    sticky = root / 'sticky-frame.html'
    sticky.write_text(common + '<div class="before"></div><div class="holder">'
                      '<iframe style="position:sticky;top:20px" src="child.html"></iframe></div>')
    floated = root / 'float-frame.html'
    floated.write_text(common + '<div class="before"></div><div class="holder">'
                       '<div style="float:left"><iframe src="child.html"></iframe></div></div>')
    for dpr in (1, 2):
        compare(f'frame-{dpr}', page, [('scroll', '0,80'), ('scroll', '0,110')], dpr)
        compare(f'edge-{dpr}', page, [('scroll', '0,160'), ('scroll', '0,250'), ('scroll', '0,280')], dpr)
        compare(f'reverse-{dpr}', page, [('scroll', '0,330'), ('scroll', '0,250')], dpr)
        compare(f'sticky-frame-{dpr}', sticky, [('scroll', '0,150'), ('scroll', '0,180')], dpr)
        compare(f'float-frame-{dpr}', floated, [('scroll', '0,80'), ('scroll', '0,110')], dpr)
        compare(f'frame-resize-{dpr}', page,
                [('scroll', '0,80'), ('resize', '760x560'), ('scroll', '0,110')], dpr)
        compare(f'child-scroll-{dpr}', page, [('scroll', '0,80'), ('scroll-point', '60,150,0,60'),
                ('scroll', '0,100'), ('scroll', '0,120')], dpr)
        compare(f'child-invalidation-{dpr}', page,
                [('scroll', '0,80'), ('batch', '2'), ('click-point', '35,60'), ('scroll', '0,100'),
                 ('scroll', '0,120')], dpr)

    transformed = root / 'unsafe-child.html'
    transformed.write_text(common + '<iframe src="transformed.html"></iframe>')
    compare('unsafe-child', transformed, [('scroll', '0,30')], last_blits=0, last_repaints=1)

    # Both directions of retained publication must refresh the sparse metadata.
    page = root / 'restyle.html'
    page.write_text(common + '''<style>#toggle:checked~.holder{display:none}
        #toggle:checked~header{position:static}</style>
        <input id="toggle" type="checkbox"><label for="toggle">Hide frame and unstick</label>
        <header>Switching header</header><div class="before"></div>
        <div class="holder"><iframe src="child.html"></iframe></div>''')
    actions = [('scroll', '0,80'), ('click-label', 'toggle'), ('scroll', '0,110')]
    compare('hidden-frame', page, actions)
    compare('shown-frame', page, actions + [('click-label', 'toggle'), ('scroll', '0,140')])

    page = root / 'overlays.html'
    page.write_text(common + '''<header>Sticky overlay</header>
        <input id="edit" style="position:absolute;top:150px;left:30px" value="Caret text">
        <select id="choose" style="position:absolute;top:150px;left:400px">
        <option>First</option><option>Second</option><option>Third</option></select>
        <p style="margin-top:270px;margin-left:30px">Select this line of text before scrolling.</p>''')
    compare('batched-scroll', page, [('scroll', '0,80'), ('batch', '2'),
            ('scroll', '0,110'), ('scroll', '0,140')], last_blits=1, last_repaints=1)
    compare('batched-resize', page, [('scroll', '0,80'), ('batch', '2'),
            ('resize', '760x560'), ('scroll', '0,110')], last_blits=0, last_repaints=1)
    compare('caret', page, [('click-id', 'edit'), ('scroll', '0,80')], last_blits=0, last_repaints=1)
    compare('caret-removed', page, [('click-id', 'edit'), ('batch', '2'),
            ('click-point', '500,400'), ('scroll', '0,80')], last_blits=0, last_repaints=1)
    steps = compare('select-menu', page, [('click-id', 'choose'), ('scroll', '0,80')],
                    last_blits=0, last_repaints=1)
    assert steps[-1]['select_menu_open'], 'select menu case did not open the menu'
    steps = compare('selection', page, [('scroll', '0,80'), ('drag', '0,10,300,10'), ('scroll', '0,110')])
    assert steps[-1].get('selection', '').strip() == 'Sticky overlay', steps[-1].get('selection')
    steps = compare('selection-body', page, [('scroll', '0,80'), ('drag', '30,230,330,230'), ('scroll', '0,110')])
    assert steps[-1].get('selection'), 'body selection case did not select text'
    wide = root / 'wide-selection.html'
    wide.write_text(page.read_text() + '<style>body{min-width:1100px}p{margin-left:150px!important}</style>')
    for dpr in (1, 2):
        steps = compare(f'selection-offsets-{dpr}', wide,
                        [('scroll', '80,80'), ('drag', '70,230,400,230'), ('scroll', '110,80')], dpr)
        assert steps[-1].get('selection'), 'selection at both scroll offsets did not select text'

print('Frame scroll reuse: nested/fixed children, floats, resize, restyle, invalidation, batching and overlays passed.')
