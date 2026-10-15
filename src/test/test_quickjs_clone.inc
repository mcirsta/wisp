/* DOM clone/ownership contracts, independent of the live site's feature tests.
 * https://dom.spec.whatwg.org/#concept-node-clone */
START_TEST(test_quickjs_clone_nodes)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "var source=document.getElementById('target'), shallow=source.cloneNode(), deep=source.cloneNode(true);"
        "check(Node.prototype.cloneNode.length===0,'arity');"
        "check(shallow!==source && shallow instanceof HTMLElement && shallow.parentNode===null,'shallow identity');"
        "check(shallow.id==='target' && shallow.className==='old' && shallow.childNodes.length===0,'attributes');"
        "check(deep.innerHTML===source.innerHTML && deep.firstChild!==source.firstChild,'deep independent children');"
        "check(!deep.isConnected && deep.ownerDocument===document && deep.firstChild.ownerDocument===document,'owner');"
        "deep.firstChild.data='changed'; deep.classList.add('new'); deep.style.color='red';"
        "check(source.textContent==='ABC' && source.className==='old' && source.style.color==='','independent mutation');"
        "for(var value of [false,undefined,null,0,'',NaN]) check(source.cloneNode(value).childNodes.length===0,'false');"
        "for(var value of [true,1,'x',{},Symbol('s'),{valueOf(){throw 1}}])"
        "check(source.cloneNode(value).childNodes.length===source.childNodes.length,'ToBoolean');"
        "for(var value of [null,{},Object.create(Node.prototype)]) {"
        "try {Node.prototype.cloneNode.call(value);throw Error('accepted bad brand')}"
        "catch(e){check(e instanceof TypeError,'receiver')}}"
        "var text=document.createTextNode('a\\0b'), comment=document.createComment('marker');"
        "check(text.cloneNode().data==='a\\0b' && comment.cloneNode().data==='marker','character data');"
        "var dt=document.firstChild.cloneNode(true);"
        "check(dt instanceof DocumentType && dt.nodeName==='html' && dt.ownerDocument===document,'doctype');"
        "var fragment=document.createDocumentFragment(); fragment.appendChild(comment);"
        "check(fragment.cloneNode().childNodes.length===0 && fragment.cloneNode(true).firstChild.data==='marker','fragment');"
        "var svg=document.getElementById('svg'), svgCopy=svg.cloneNode(true);"
        "check(svgCopy instanceof SVGElement && svgCopy.innerHTML===svg.innerHTML,'SVG namespace');"
        "source.extra=42; var calls=0; source.addEventListener('probe',()=>calls++);"
        "var listenerCopy=source.cloneNode(true); listenerCopy.dispatchEvent(new Event('probe'));"
        "check(calls===0 && listenerCopy.extra===undefined,'listeners and expandos excluded');"
        "source.dispatchEvent(new Event('probe')); check(calls===1,'original listener preserved');"
        "var styled=deep.cloneNode(true); check(styled.style.color==='red' && styled.style!==deep.style,'style copied via attribute');",
        "clone node kinds, conversion and independent state"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

START_TEST(test_quickjs_clone_documents)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "var copy=document.cloneNode(true), empty=document.cloneNode();"
        "check(copy!==document && copy instanceof HTMLDocument && copy.ownerDocument===null,'document identity');"
        "check(copy.parentNode===null && copy.isConnected && copy.documentElement.isConnected,'document rooted');"
        "check(empty.childNodes.length===0 && empty.documentElement===null && empty.readyState==='complete','empty document');"
        "check(copy.documentElement.ownerDocument===copy && copy.head.parentNode===copy.documentElement,'child owners');"
        "check(copy.getElementById('target')===copy.body.firstChild,'cache identity');"
        "check(copy.body.innerHTML===document.body.innerHTML,'deep document');"
        "var a=copy.createElement('nav'); a.textContent='copy'; copy.body.appendChild(a);"
        "check(a.ownerDocument===copy && a.isConnected && copy.getElementsByTagName('nav')[0]===a,'new children');"
        "var calls=0; window.addEventListener('probe',()=>calls++); document.addEventListener('probe',()=>calls++);"
        "a.dispatchEvent(new Event('probe',{bubbles:true})); check(calls===0,'no original document/window event parent');"
        "var detached=document.createDocumentFragment(); detached.appendChild(a);"
        "check(a.ownerDocument===document && !a.isConnected && detached.firstChild===a,'adoption updates owner and cache');"
        "copy.body.appendChild(a); check(a.ownerDocument===copy && a.isConnected,'adoption back');"
        "var ns=copy.createElement('noscript'); ns.innerHTML='<b>x</b>';"
        "check(ns.firstChild.nodeType===1 && ns.innerHTML==='<b>x</b>','scripting disabled in document clone');"
        "var second=copy.cloneNode(true); check(second.body.lastChild.textContent==='copy','clone of clone');"
        "check(second.body.lastChild.ownerDocument===second,'second owner');"
        "empty.appendChild(empty.createElement('html')); check(empty.documentElement.ownerDocument===empty,'empty mutable');",
        "clone documents, ownership, adoption and browsing-context isolation"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

START_TEST(test_quickjs_clone_templates)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "var t=document.createElement('template'); t.innerHTML='<span>A</span><template><b>B</b></template>';"
        "var c=t.cloneNode(true), s=t.cloneNode();"
        "check(c.innerHTML===t.innerHTML && s.innerHTML==='' && c.childNodes.length===0,'template contents cloned');"
        "c.innerHTML='<em>C</em>'; check(t.innerHTML.indexOf('<span>A</span>')===0,'independent contents');"
        "var doc=document.cloneNode(), root=doc.createElement('html'); doc.appendChild(root); root.appendChild(t);"
        "var copy=doc.cloneNode(true); check(copy.documentElement.firstChild.innerHTML===t.innerHTML,'template in document');",
        "template cloning and inert ownership"));
}
END_TEST

START_TEST(test_quickjs_clone_wrapper_failure)
{
    JSContext *ctx = dom_thread->ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue doc = JS_GetPropertyStr(ctx, global, "document");
    JSValue clone = JS_GetPropertyStr(ctx, doc, "cloneNode");
    /* Native cloning can succeed while allocating its JS wrapper fails. Each
     * partial wrapper/document must be released, and the realm stay usable. */
    bool succeeded = false;
    for (size_t extra = 0; extra <= 4096; extra += 64) {
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(dom_heap->rt, &usage);
        JS_SetMemoryLimit(dom_heap->rt, usage.malloc_size + extra);
        JSValue deep = JS_TRUE;
        JSValue result = JS_Call(ctx, clone, doc, 1, &deep);
        bool failed = JS_IsException(result);
        JS_SetMemoryLimit(dom_heap->rt, 0);
        if (failed)
            JS_FreeValue(ctx, JS_GetException(ctx));
        JS_FreeValue(ctx, result);
        ck_assert_msg(extra != 0 || failed, "Exhausted wrapper allocation budget was ignored");
        JS_RunGC(dom_heap->rt);
        if (!failed) {
            succeeded = true;
            break;
        }
    }
    ck_assert(succeeded);
    JS_FreeValue(ctx, clone);
    JS_FreeValue(ctx, doc);
    JS_FreeValue(ctx, global);
    assert_predicate(dom_thread, "document.cloneNode(true).body.ownerDocument!==document", "clone after OOM");
}
END_TEST

START_TEST(test_quickjs_clone_retirement)
{
    ck_assert(exec_dom_script(dom_thread,
        "var doc=document.cloneNode(true), child=doc.body; doc.cycle={child,doc}; child.extra=doc;",
        "cloned document cycles"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "child.ownerDocument===doc && doc.cycle.child===child", "cloned document lifetime");
    JSContext *ctx = dom_thread->ctx;
    JSValue global = JS_GetGlobalObject(ctx), doc = JS_GetPropertyStr(ctx, global, "doc");
    JSValue clone = JS_GetPropertyStr(ctx, doc, "cloneNode");
    js_closethread(dom_thread);
    JSValue result = JS_Call(ctx, clone, doc, 0, NULL);
    ck_assert(JS_IsException(result));
    JSValue error = JS_GetException(ctx), name = JS_GetPropertyStr(ctx, error, "name");
    const char *text = JS_ToCString(ctx, name);
    ck_assert_str_eq(text, "InvalidStateError");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, name);
    JS_FreeValue(ctx, error);
    JS_FreeValue(ctx, clone);
    JS_FreeValue(ctx, doc);
    JS_FreeValue(ctx, global);
}
END_TEST

static void add_clone_tests(TCase *tc)
{
    tcase_add_test(tc, test_quickjs_clone_nodes);
    tcase_add_test(tc, test_quickjs_clone_documents);
    tcase_add_test(tc, test_quickjs_clone_templates);
    tcase_add_test(tc, test_quickjs_clone_wrapper_failure);
    tcase_add_test(tc, test_quickjs_clone_retirement);
}
