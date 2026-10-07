#!/usr/bin/env python3
"""Real DOM edits must change boxes and pixels, with attributable JS logging."""
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
prefix = '''<!doctype html><meta charset="utf-8"><title>DOM mutation fixture</title>
<style>body { margin: 0; font: 20px/30px monospace; }
.old { width: 120px; height: 40px; background: red; }
.new { width: 240px; height: 80px; background: #20a060; color: white; }
#reveal[hidden] { display: none; } #reveal { height: 35px; background: #ffee66; }
</style>'''
initial = '<div id="target" class="old">Before <span>child</span></div><div id="reveal" hidden>Revealed</div>'
expected = '<div id="target" class="new">Changed ș😀</div><div id="reveal">Revealed</div>'
mutation = '''const target = document.getElementById('target');
if (target !== document.body.firstChild || target !== document.getElementById('target')) throw Error('identity');
target.className = 'new';
Promise.resolve().then(() => {
    target.textContent = 'Changed ș😀';
    document.getElementById('reveal').removeAttribute('hidden');
    console.log('DOM_CHANGED');
});'''


def run(root, name, body, js=True, missing=False):
    page = root / (name + '.html')
    page.write_text(prefix + body)
    dest = root / name
    result = subprocess.run([str(binary), '-v', '--js_trace=1', '--script_timeout=1',
                             f'--enable_javascript={int(js)}', '--capture-dir', str(dest),
                             '--capture-size', '500x300', '--capture-id', 'target', '--capture-id', 'reveal',
                             '--capture-settle', '600', '--capture-timeout', '6000', str(page)],
                            env=env, capture_output=True, text=True, timeout=10)
    log = result.stdout + result.stderr
    diagnostic = '\n'.join(line for line in log.splitlines() if '[JS ' in line or 'JavaScript ' in line or 'Console:' in line)
    assert result.returncode == 0 and 'runtime error:' not in log, (name, diagnostic, log[-8000:])
    meta = json.loads((dest / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], (name, meta, diagnostic)
    inspection = json.loads((dest / 'inspection.json').read_text())
    if inspection['status'] == 'layout_unavailable':
        assert missing and meta['document_extent'] == {'width': 0, 'height': 0}, (name, meta, diagnostic)
        return dest, [], diagnostic
    elements = inspection['elements']
    assert all(e['status'] in (('not_found',) if missing else ('ok', 'no_box')) for e in elements), (name, elements)
    return dest, elements, diagnostic


with tempfile.TemporaryDirectory(prefix='wisp-js-dom-') as tmp:
    root = Path(tmp)
    reference, reference_elements, _ = run(root, 'reference', expected, False)
    initial_capture, _, _ = run(root, 'disabled', initial + '<script>' + mutation + '</script>', False)
    assert (initial_capture / 'page.png').read_bytes() != (reference / 'page.png').read_bytes()
    for name, script in (
            ('inline', mutation),
            ('timer', 'setTimeout(() => {' + mutation + '}, 150);'),
            ('classlist', 'setTimeout(() => {' + mutation.replace("target.className = 'new';", """
                target.classList.add('new', 'temporary');
                target.classList.remove('old', 'temporary');
                target.classList.toggle('unused', false);
                target.classList.toggle('new', true);
                target.classList.replace('missing', 'also-missing');
            """) + '}, 150);'),
            ('exception', mutation + "throw Error('Expected after mutation');"),
            ('interrupted', "setTimeout(() => {" + mutation.replace('Promise.resolve().then(() => {', '{').replace('});', '}') +
             'while (true) {} }, 150);')):
        dest, elements, log = run(root, name, initial + '<script>' + script + '</script>')
        assert 'Console: DOM_CHANGED' in log, (name, log)
        assert (dest / 'page.png').read_bytes() == (reference / 'page.png').read_bytes(), (name, elements, log)
        for actual, expected_element in zip(elements, reference_elements):
            assert actual['boxes'] and expected_element['boxes'], (name, actual, expected_element)
            assert actual['boxes'][0]['border_box'] == expected_element['boxes'][0]['border_box'], (name, actual)
        changes = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] dom.mutation (.*)', log)
        requests = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] render.request (.*)', log)
        count = 4 if name == 'classlist' else 3
        assert len(changes) == count and len(requests) == 1, (name, log)
        assert all(doc == requests[0][0] and entry == requests[0][1] for doc, entry, _ in changes), (name, log)
        assert f'changes={count}' in requests[0][2], (name, log)
        if name == 'classlist':
            assert 'operation=classList.add' in log and 'operation=classList.remove' in log, log
            assert 'operation=classList.toggle' not in log and 'operation=classList.replace' not in log, log
        assert re.search(r'dom.node node=\d+ type=1 name=DIV id=target', log), (name, log)
        assert 'operation=removeAttribute attribute=hidden result=0' in log, (name, log)
        assert ('mode=initial-build' if name in ('inline', 'exception') else 'mode=full-restyle') in requests[0][2], (name, log)
        if name == 'interrupted':
            assert 'JavaScript execution limit reached' in log and 'end result=interrupted' in log, log

    # Explicit script dispatch nests listener entries inside the timer. The DOM
    # effects and queued job must coalesce into the same static reference pixels.
    dest, _, log = run(root, 'event-mutation', initial + '''<script>
        const source = new EventTarget();
        source.addEventListener('change', function(e) {
            if (e.target !== source || e.detail !== 42) throw Error('event identity');
            document.getElementById('target').className = 'new';
            queueMicrotask(() => {
                document.getElementById('target').textContent = 'Changed ș😀';
                document.getElementById('reveal').removeAttribute('hidden');
                console.log('EVENT_CHANGED');
            });
        }, {once: true});
        setTimeout(() => {
            source.dispatchEvent(new CustomEvent('change', {detail: 42}));
            source.dispatchEvent(new CustomEvent('change', {detail: 'must not run'}));
        }, 150);
    </script>''')
    assert 'Console: EVENT_CHANGED' in log and 'JavaScript exception' not in log, log
    assert (dest / 'page.png').read_bytes() == (reference / 'page.png').read_bytes(), log
    assert len(re.findall(r'\] render.request ', log)) == 1, log
    assert len(re.findall(r'\] event.listener ', log)) == 1, log
    event_entry = re.search(r'\[JS doc=(\d+) entry=(\d+)\] begin kind=event ', log)
    assert event_entry, log
    doc, entry = event_entry.groups()
    assert f'[JS doc={doc} entry={entry}] dom.mutation ' in log, log
    assert f'render.request first_entry={entry} changes=3 mode=full-restyle' in log, log

    tree_edit = '''
        const old = document.getElementById('target');
        const target = document.createElement('div');
        target.id = 'target'; target.className = 'new';
        const fragment = document.createDocumentFragment();
        fragment.appendChild(document.createTextNode('Changed ș😀'));
        target.appendChild(fragment);
        if (fragment.firstChild !== null) throw Error('fragment not emptied');
        document.body.replaceChild(target, old);
        const temporary = document.createElement('span'); temporary.textContent = 'temporary';
        target.insertBefore(temporary, target.firstChild);
        target.removeChild(temporary);
        document.getElementById('reveal').removeAttribute('hidden');
        console.log('TREE_CHANGED');
    '''
    for name, script in (('tree-inline', tree_edit), ('tree-timer', 'setTimeout(() => {' + tree_edit + '}, 150);')):
        dest, elements, log = run(root, name, initial + '<script>' + script + '</script>')
        assert 'Console: TREE_CHANGED' in log, (name, log)
        assert (dest / 'page.png').read_bytes() == (reference / 'page.png').read_bytes(), (name, elements, log)
        assert len(re.findall(r'\] render.request ', log)) == 1, (name, log)
        for operation in ('appendChild', 'insertBefore', 'replaceChild', 'removeChild'):
            assert f'dom.tree operation={operation}' in log, (name, log)
        assert 'mode=' + ('initial-build' if name == 'tree-inline' else 'full-restyle') in log, log

    # Only the source parent is connected. Invalidating just the destination
    # would leave the removed text painted until some unrelated page update.
    moved_reference, _, _ = run(root, 'move-reference', initial.replace('<span>child</span>', ''), False)
    dest, elements, log = run(root, 'move-to-detached', initial + '''<script>
        setTimeout(() => {
            const fragment = document.createDocumentFragment();
            fragment.appendChild(document.getElementById('target').firstChild.nextSibling);
        }, 150);
    </script>''')
    assert (dest / 'page.png').read_bytes() == (moved_reference / 'page.png').read_bytes(), (elements, log)
    assert 'operation=moveFrom' in log and 'changes=1 mode=full-restyle' in log, log
    assert re.search(r'dom.tree operation=appendChild .* former_parent=[1-9]\d* result=0', log), log

    dest, _, log = run(root, 'detached-only', initial + '''<script>
        setTimeout(() => {
            const fragment = document.createDocumentFragment(), child = document.createElement('div');
            child.textContent = 'Detached'; fragment.appendChild(child);
            fragment.replaceChild(document.createTextNode('replacement'), child);
        }, 150);
    </script>''')
    assert (dest / 'page.png').read_bytes() == (initial_capture / 'page.png').read_bytes(), log
    assert 'dom.tree' in log and 'render.request' not in log, log

    dest, _, log = run(root, 'tree-noop', initial + '''<script>
        setTimeout(() => {
            const parent = document.getElementById('target'), child = parent.firstChild;
            parent.appendChild(document.createDocumentFragment());
            parent.insertBefore(child, child); parent.replaceChild(child, child);
            try { parent.insertBefore(parent, child); throw Error('cycle allowed'); }
            catch(e) { if (e.name !== 'HierarchyRequestError') throw e; }
        }, 150);
    </script>''')
    assert (dest / 'page.png').read_bytes() == (initial_capture / 'page.png').read_bytes(), log
    assert 'dom.tree' in log and 'render.request' not in log, log

    # The same saved collections follow subsequent parser writes, then timer
    # edits. Collection reads alone must not request a renderer refresh.
    dest, elements, log = run(root, 'live-collections', initial + """<script>
        var nodes = document.body.childNodes, children = document.body.children;
        var oldLength = nodes.length;
        setTimeout(() => {
            if (nodes !== document.body.childNodes || children !== document.body.children ||
                nodes.length <= oldLength || !children.later) throw Error('parser liveness');
            document.body.removeChild(children.later);
            const target = children.target, contents = target.childNodes;
            while (contents.length) target.removeChild(contents[0]);
            target.appendChild(document.createTextNode('Changed ș😀')); target.className = 'new';
            children.reveal.removeAttribute('hidden'); console.log('COLLECTIONS_CHANGED');
        }, 150);
    </script><p id="later">Parser appended this after saving the collections</p>""")
    assert 'Console: COLLECTIONS_CHANGED' in log, log
    assert (dest / 'page.png').read_bytes() == (reference / 'page.png').read_bytes(), (elements, log)
    assert len(re.findall(r'\] render.request ', log)) == 1, log
    dest, _, log = run(root, 'collection-reads', initial + """<script>
        setTimeout(() => {
            const nodes = document.body.childNodes, children = document.body.children;
            if ([...nodes].length !== nodes.length || !children.namedItem('target') ||
                !Object.keys(children).length) throw Error('read collections');
            console.log('COLLECTIONS_READ');
        }, 150);
    </script>""")
    assert 'Console: COLLECTIONS_READ' in log and 'render.request' not in log, log
    assert (dest / 'page.png').read_bytes() == (initial_capture / 'page.png').read_bytes(), log

    # A missing document element is legal after script changes. It must clear
    # the old presentation and still allow a later timer to restore the root.
    blank, _, _ = run(root, 'blank-reference', '', False, missing=True)
    remove_root = 'var savedRoot = document.removeChild(document.documentElement);'
    for name, script in (('root-inline', remove_root),
                         ('root-timer', 'setTimeout(() => {' + remove_root + '}, 150);')):
        dest, _, log = run(root, name, initial + '<script>' + script + '</script>', missing=True)
        assert (dest / 'page.png').read_bytes() == (blank / 'page.png').read_bytes(), (name, log)
    dest, _, log = run(root, 'body-remove', '<style>body { background: #20a060; }</style>' + initial + '''<script>
        setTimeout(() => document.documentElement.removeChild(document.body), 150);
    </script>''', missing=True)
    assert (dest / 'page.png').read_bytes() == (blank / 'page.png').read_bytes(), log
    for name, script in (
            ('root-restore-inline', remove_root + 'setTimeout(() => document.appendChild(savedRoot), 150);'),
            ('root-restore-timer', 'setTimeout(() => {' + remove_root +
             'setTimeout(() => document.appendChild(savedRoot), 150); }, 150);')):
        dest, _, log = run(root, name, initial + '<script>' + script + '</script>')
        assert (dest / 'page.png').read_bytes() == (initial_capture / 'page.png').read_bytes(), (name, log)
        assert 'mode=full-restyle' in log, (name, log)

print('JS DOM/tree edits, live collections, coalesced rendering, pixels and trace attribution pass.')
