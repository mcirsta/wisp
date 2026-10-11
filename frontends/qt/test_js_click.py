#!/usr/bin/env python3
"""Qt press/release -> trusted JS click -> cancellation, DOM updates and native actions."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
style = '''<!doctype html><meta charset="utf-8"><style>
body { margin:0; font:20px/30px monospace; }
#target { display:block; position:absolute; left:20px; top:20px; width:120px; height:40px; background:#eee; }
#result { position:absolute; left:20px; top:120px; }
</style>'''


def run(root, name, body, clicks=(), js=True, actions=()):
    page = root / (name + '.html')
    page.write_text(style + body)
    dest = root / name
    args = [str(binary), '-v', '--js_trace=1', '--script_timeout=1',
            f'--enable_javascript={int(js)}', '--capture-dir', str(dest), '--capture-size', '500x300',
            '--capture-settle', '100', '--capture-timeout', '6000', '--core_select_menu=1',
            '--capture-id', 'target', '--capture-id', 'result', '--capture-id', 'box']
    for click in clicks:
        args += ['--capture-click-id', click]
    for kind, value in actions:
        args += ['--capture-' + kind, value]
    result = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=10)
    log = result.stdout + result.stderr
    assert result.returncode == 0 and 'runtime error:' not in log, (name, log[-10000:])
    diagnostic = '\n'.join(line for line in log.splitlines() if '[JS ' in line or 'JavaScript ' in line or 'Console:' in line)
    assert 'Error:' not in diagnostic, (name, diagnostic)
    meta = json.loads((dest / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], (name, meta, diagnostic)
    steps = json.loads((dest / 'steps.json').read_text()) if clicks or actions else []
    return dest, diagnostic, steps


with tempfile.TemporaryDirectory(prefix='wisp-js-click-') as tmp:
    root = Path(tmp)
    markup = '<div id="target">Click</div><div id="result">Before</div>'
    script = '''<script>
        var seen=[], target=document.getElementById('target');
        window.addEventListener('click',()=>seen.push('capture'),true);
        target.addEventListener('click',e=>{
            if (!(e instanceof PointerEvent) || !e.isTrusted || e.target!==target || e.view!==window ||
                e.clientX!==80 || e.clientY!==40 || e.buttons!==0 || e.button!==0 || e.detail!==1 ||
                e.pointerType!=='mouse' || !e.isPrimary) throw Error('native data');
            seen.push('target'); queueMicrotask(()=>{seen.push('microtask');e.preventDefault();});
        });
        document.addEventListener('click',e=>{
            if(!e.defaultPrevented || seen.join()!=='capture,target,microtask')throw Error('callback order');
            document.getElementById('result').textContent='Clicked'; console.log('CLICK_PASS');
        });
    </script>'''
    reference, _, _ = run(root, 'reference', markup.replace('Before', 'Clicked'), js=False)
    actual, log, _ = run(root, 'basic', markup + script, ['target'])
    assert 'Console: CLICK_PASS' in log, log
    assert 'input.dispatch type=click origin=browser' in log, log
    assert re.search(r'\[JS doc=\d+ entry=[1-9]\d*\] dom.mutation .*operation=textContent', log), log
    assert (actual / 'page.png').read_bytes() == (reference / 'page.png').read_bytes(), log
    disabled, log, _ = run(root, 'disabled', markup + script, ['target'], js=False)
    assert 'CLICK_PASS' not in log and 'input.dispatch' not in log, log
    assert (disabled / 'page.png').read_bytes() != (reference / 'page.png').read_bytes()

    _, log, steps = run(root, 'checkable', '''<label id="target" for="box">Toggle</label>
        <input id="box" type="checkbox" style="position:absolute;top:80px"><script>
        var box=document.getElementById('box'), seen=[], count=0;
        document.addEventListener('click',e=>seen.push(e.target.id));
        box.addEventListener('click',e=>{if(++count===1)e.preventDefault();});
        box.addEventListener('input',e=>{
            if(!e.isTrusted || !e.composed || e.cancelable || e.constructor!==Event)throw Error('input');
            seen.push('input');
        });
        box.addEventListener('change',e=>{
            if(!e.isTrusted || e.composed || e.cancelable)throw Error('change');seen.push('change');
            if(seen.join()!=='target,box,target,box,input,change')throw Error('label order '+seen);
            console.log('CHECKABLE_PASS');
        });</script>''', ['target', 'target'])
    states = [next(e for e in step['elements'] if e['id']=='box')['state']['checked'] for step in steps]
    assert states == [False, False, True], (states, log)
    assert 'Console: CHECKABLE_PASS' in log, log

    _, log, _ = run(root, 'disabled-control', '''<button disabled id="target">Disabled</button><script>
        document.addEventListener('click',()=>{throw Error('disabled control clicked');});</script>''', ['target'])

    # Both picker types used to open before cancellation. Removing the control
    # during its listener must also avoid using the old presentation afterward.
    for control in ('<select id="target"><option>One</option></select>', '<input id="target" type="file">'):
        name = 'select' if control.startswith('<select') else 'file'
        dest, log, _ = run(root, name, control + '''<div id="result">Before</div><script>
            document.getElementById('target').addEventListener('click',e=>{
                e.preventDefault();e.target.parentNode.removeChild(e.target);
                document.getElementById('result').textContent='Removed';console.log('PICKER_PASS');
            });</script>''', ['target'])
        assert 'Console: PICKER_PASS' in log, log
        assert not json.loads((dest / 'inspection.json').read_text())['select_menu_open'], (name, log)

    destination = root / 'destination.html'
    destination.write_text(style + '<div id="result">Destination</div>')
    wrong = root / 'wrong.html'
    wrong.write_text('<script>console.log("WRONG_DESTINATION");</script>')
    for name, action in (
            ('cancel-link', "e.preventDefault();console.log('CANCEL_PASS');"),
            ('edit-link', f"e.target.setAttribute('href',{json.dumps(destination.as_uri())});console.log('EDIT_PASS');"),
            ('navigate', f"e.preventDefault();location.assign({json.dumps(destination.as_uri())});console.log('NAVIGATE_PASS');"),
            ('remove', "e.target.parentNode.removeChild(e.target);e.preventDefault();console.log('REMOVE_PASS');")):
        _, log, _ = run(root, name, f'<a id="target" href="{wrong.as_uri()}">Go</a><script>' +
            "document.getElementById('target').addEventListener('click',e=>{" + action + "});</script>", ['target'])
        assert 'Console: ' + name.split('-')[0].upper() + '_PASS' in log, log
        assert 'Console: WRONG_DESTINATION' not in log, log

    # The form callback APIs are a later milestone, but canceling the click must
    # already suppress its ordinary native submission.
    _, log, _ = run(root, 'cancel-submit', f'<form action="{wrong.as_uri()}"><button id="target">Send</button></form>' + '''
        <script>document.getElementById('target').addEventListener('click',e=>{
            e.preventDefault();console.log('SUBMIT_CANCEL_PASS');});</script>''', ['target'])
    assert 'Console: SUBMIT_CANCEL_PASS' in log and 'Console: WRONG_DESTINATION' not in log, log

    frame = root / 'frame.html'
    frame.write_text(style + '<div id="target">Child</div><script>' +
        "document.getElementById('target').addEventListener('click',e=>{"
        "if(e.clientX!==80||e.clientY!==40||e.view!==window)throw Error('frame coordinates');"
        "console.log('FRAME_PASS');});</script>")
    _, log, _ = run(root, 'frame-parent',
        f'<iframe src="{frame.as_uri()}" style="position:absolute;left:20px;top:20px;width:200px;height:100px;border:0"></iframe>' +
        "<script>document.addEventListener('click',()=>{throw Error('parent received child click');});</script>",
        actions=[('click-point','100,60')])
    assert 'Console: FRAME_PASS' in log, log

    # Pointer coordinates are relative to the viewport after scrolling.
    _, log, _ = run(root, 'scrolled', markup + '''<div style="height:1500px"></div><script>
        document.getElementById('target').setAttribute('style','top:620px');
        document.getElementById('target').addEventListener('click',e=>{
            if(e.clientX!==80 || e.clientY!==40)throw Error('scrolled coordinates '+e.clientY);
            console.log('SCROLL_PASS');});</script>''', actions=[('scroll','0,600'),('click-point','80,40')])
    assert 'Console: SCROLL_PASS' in log, log

print('Qt native clicks: payloads, ordering, pixels, logging, disabled JS/controls, checkables, pickers and navigation pass.')
