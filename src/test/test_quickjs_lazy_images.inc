/* Shared DOM contracts required by the actual Tion/Bihon lazySizes loader.
 * https://dom.spec.whatwg.org/#concept-getelementsbyclassname
 * https://dom.spec.whatwg.org/#dom-document-createevent */
START_TEST(test_quickjs_class_queries)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "var root=document.createElement('section'); root.className='lazyload';"
        "root.innerHTML='<img id=a class=lazyload><div><img id=b class=\"lazyload lazypreload\"></div>' +"
        "'<svg><text id=c class=lazyload></text></svg><template><img class=lazyload></template>';"
        "var all=root.getElementsByClassName('lazyload'), pre=root.getElementsByClassName('lazyload lazypreload');"
        "check(all instanceof HTMLCollection && all.length===3 && pre.length===1,'descendants and all tokens');"
        "check(all[0].id==='a' && all[1]===pre[0] && all[2].id==='c','tree order and foreign namespace');"
        "check(all.namedItem('b')===pre[0] && all.c===all[2] && Object.keys(all).join()==='0,1,2','collection properties');"
        "check(root.getElementsByClassName(' \\tlazyload\\rlazyload\\n\\f ').length===3,'ASCII whitespace and duplicates');"
        "check(root.getElementsByClassName('').length===0 && root.getElementsByClassName(' \\n').length===0,'empty');"
        "check(root.getElementsByClassName('LAZYLOAD').length===0,'standard case');"
        "var first=all[0]; first.className='done'; check(all.length===2 && all[0]===pre[0],'live class changes');"
        "first.classList.add('lazyload'); check(all[0]===first,'live insertion back');"
        "var iterator=all[Symbol.iterator](); check(iterator.next().value===first,'iterator');"
        "first.parentNode.removeChild(first); check(all.length===2 && iterator.next().value.id==='c','live iteration');"
        "document.body.appendChild(root); check(document.getElementsByClassName('lazyload').length===3,'document root');"
        "check(root.getElementsByClassName('lazyload\\u00a0lazypreload').length===0,'NBSP not whitespace');"
        "check(!('getElementsByClassName' in document.createDocumentFragment()),'interface placement');"
        "first.className='a\\0b'; root.appendChild(first);"
        "check(root.getElementsByClassName('a\\0b')[0]===first && root.getElementsByClassName('a').length===0,'embedded NUL');"
        "var copy=document.cloneNode(); copy.appendChild(root);"
        "check(all.length===2 && copy.getElementsByClassName('lazyload').length===3,'adoption retains collection');"
        "var kept=root.getElementsByClassName('a\\0b'); root=null; first=null;",
        "live class queries"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "kept.length===1 && kept[0].ownerDocument===copy", "class query roots survive GC");
}
END_TEST

START_TEST(test_quickjs_class_query_conversion)
{
    ck_assert(exec_dom_script(dom_thread,
        "function raises(fn){try{fn()}catch(e){if(e instanceof TypeError)return;throw e}throw Error('missing TypeError')}"
        "raises(()=>document.getElementsByClassName()); raises(()=>document.getElementsByClassName(Symbol()));"
        "var converted=false, value={toString(){converted=true;return 'old'}};"
        "raises(()=>Document.prototype.getElementsByClassName.call(document.body,value));"
        "raises(()=>Element.prototype.getElementsByClassName.call(document,value));"
        "raises(()=>Element.prototype.getElementsByClassName.call({},value));"
        "if(converted)throw Error('converted before brand check');"
        "if(document.getElementsByClassName(value)[0]!==document.getElementById('target')||!converted)throw Error('conversion');"
        "var p=document.createElement('div');p.innerHTML='<i class=null></i><b class=undefined></b>';"
        "if(p.getElementsByClassName(null)[0]!==p.firstChild||p.getElementsByClassName(undefined)[0]!==p.lastChild)"
        "throw Error('DOMString null/undefined');",
        "class query Web IDL conversion"));
}
END_TEST

START_TEST(test_quickjs_create_event)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "var groups=[[Event,'Event','Events','HTMLEvents','SVGEvents'],[CustomEvent,'CustomEvent'],"
        "[UIEvent,'UIEvent','UIEvents'],[MouseEvent,'MouseEvent','MouseEvents']];"
        "for(var group of groups)for(var name of group.slice(1)) {"
        "var e=document.createEvent(name.toUpperCase());"
        "check(Object.getPrototypeOf(e)===group[0].prototype,'interface '+name);"
        "check(e.type==='' && !e.isTrusted && !e.bubbles && !e.cancelable && !e.composed,'defaults '+name);"
        "check(e.target===null && e.currentTarget===null && e.eventPhase===0 && !e.defaultPrevented,'dispatch state');"
        "check(e.timeStamp>=0 && e.timeStamp<=new Event('clock').timeStamp,'timestamp');"
        "try{document.dispatchEvent(e);throw Error('accepted uninitialized event')}"
        "catch(error){check(error.name==='InvalidStateError' && e.target===null,'initialization required')}"
        "e.initEvent('probe',false,true);check(document.dispatchEvent(e),'dispatch after initialization');"
        "}"
        "var detail={width:80}, event=document.createEvent('CustomEvent'), calls=0;"
        "check(event.detail===null,'custom default detail');"
        "event.initCustomEvent('lazybeforesizes',true,true,detail);"
        "var target=document.getElementById('target'); target.addEventListener('lazybeforesizes',function(e){"
        "check(e===event && e.detail===detail && e.target===target,'custom detail and identity');e.preventDefault();calls++});"
        "check(!target.dispatchEvent(event) && calls===1,'cancelable loader event');"
        "var mouse=document.createEvent('MouseEvents'), ui=document.createEvent('UIEvents');"
        "check(mouse.view===null && mouse.relatedTarget===null && mouse.clientX===0 && mouse.button===0 && mouse.buttons===0,'mouse defaults');"
        "check(ui.detail===0 && ui.view===null,'UI defaults');"
        "var proto=CustomEvent.prototype; window.CustomEvent=function(){throw Error('page constructor called')};"
        "check(Object.getPrototypeOf(document.createEvent('CustomEvent'))===proto,'factory intrinsic prototype');"
        "check(document.cloneNode().createEvent('Event') instanceof Event,'inactive document');",
        "createEvent aliases, defaults, initialization and real dispatch"));
}
END_TEST

START_TEST(test_quickjs_create_event_errors)
{
    ck_assert(exec_dom_script(dom_thread,
        "function raises(fn,name){try{fn()}catch(e){if(e.name===name)return;throw e}throw Error('missing '+name)}"
        "raises(()=>document.createEvent(),'TypeError');raises(()=>document.createEvent(Symbol()),'TypeError');"
        "var converted=false; raises(()=>Document.prototype.createEvent.call(document.body,{toString(){converted=true;return 'Event'}}),'TypeError');"
        "if(converted)throw Error('conversion before brand');"
        "for(var name of ['',null,undefined,' Event','Event ','Event\\0','CustomEvents','PointerEvent','SubmitEvent','KeyboardEvent','NoEvent'])"
        "raises(()=>document.createEvent(name),'NotSupportedError');"
        "var marker={};try{document.createEvent({toString(){throw marker}})}catch(e){if(e!==marker)throw Error('conversion exception')}"
        "if(!(document.createEvent({toString(){return 'CuStOmEvEnT'}}) instanceof CustomEvent))throw Error('conversion');",
        "event factory errors and conversion"));
}
END_TEST

START_TEST(test_quickjs_loader_foundation_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
                         JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)),
        1);
    JS_FreeValue(dom_thread->ctx, global);
    const char *scripts[] = {"document.getElementsByClassName({toString(){retire();return 'old'}})",
        "document.createEvent({toString(){retire();return 'CustomEvent'}})",
        "(()=>{var c=document.getElementsByClassName('old');retire();return c.length})()",
        "(()=>{var c=document.getElementsByClassName('old');retire();return c[0]})()"};
    char source[512];
    snprintf(source, sizeof(source),
        "try{%s;throw Error('accepted retired document')}catch(e){"
        "if(e.name!=='InvalidStateError')throw e}",
        scripts[_i]);
    ck_assert_msg(exec_dom_script(dom_thread, source, "loader foundation retirement"), "variant %d", _i);
}
END_TEST

START_TEST(test_quickjs_real_lazysizes_initialization)
{
    setup_timers();
    FILE *file = fopen(WISP_TEST_LAZY_LOADER, "rb");
    ck_assert_ptr_nonnull(file);
    ck_assert_int_eq(fseek(file, 0, SEEK_END), 0);
    long length = ftell(file);
    ck_assert_msg(length > 0 && length < 65536, "unexpected lazySizes fixture length %ld", length);
    rewind(file);
    char *script = malloc((size_t)length + 1);
    ck_assert_ptr_nonnull(script);
    ck_assert_uint_eq(fread(script, 1, length, file), (size_t)length);
    fclose(file);
    script[length] = 0;
    /* Exercise the unmodified loader and its actual event helper. Defer its
     * geometry/timer pass to the rendered-page tests, not mock DOM geometry. */
    ck_assert(exec_dom_script(dom_thread, "var lazySizesConfig={init:false};", "loader config"));
    bool loaded = exec_dom_script(dom_thread, script, "preserved lazySizes 4.0.3");
    free(script);
    ck_assert(loaded);
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "check(lazySizes && lazySizes.cfg.lazyClass==='lazyload','loader initialized');"
        "lazySizes.init(); check(lazySizes.elements instanceof HTMLCollection && !lazySizes.elements.length,'live query');"
        "var img=document.createElement('img'); img.className='lazyload'; document.body.appendChild(img);"
        "check(lazySizes.elements[0]===img,'real loader observes insertion');"
        "var detail={width:200}, called=0; img.addEventListener('lazybeforesizes',function(e){"
        "check(e.detail===detail && e.detail.instance===lazySizes && !e.isTrusted,'loader event data');"
        "called++;e.preventDefault()});"
        "check(lazySizes.fire(img,'lazybeforesizes',detail).defaultPrevented && called===1,'loader cancelation');"
        "lazySizes.rC(img,'lazyload'); check(!lazySizes.elements.length,'real loader observes class removal');",
        "actual loader collection and event path"));
    js_closethread(dom_thread);
    teardown_timers();
}
END_TEST

START_TEST(test_quickjs_image_interface)
{
    dom_string *uri = NULL;
    const char *base = "https://example.test/dir/page.html";
    ck_assert_int_eq(dom_string_create((const uint8_t *)base, strlen(base), &uri), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_set_uri(dom_html.document, uri), DOM_NO_ERR);
    dom_string_unref(uri);
    /* Inert documents expose reflection but do not start network requests.
     * Real fetching, completion and DOM ownership are covered by Qt/HTTP. */
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "function raises(fn){try{fn()}catch(e){if(e instanceof TypeError)return;throw e}throw Error('missing TypeError')}"
        "var inactive=document.cloneNode(false), img=inactive.createElement('img');"
        "check(img instanceof HTMLImageElement && img instanceof HTMLElement,'image prototype');"
        "raises(()=>new HTMLImageElement());"
        "check(img.src==='' && img.srcset==='' && img.sizes==='' && img.complete && img.currentSrc==='' &&"
        "img.naturalWidth===0 && img.naturalHeight===0,'defaults');"
        "img.src='../photo.png';check(img.getAttribute('src')==='../photo.png' && img.src==='https://example.test/photo.png','URL reflection');"
        "check(!img.complete && img.naturalWidth===0,'inactive unavailable image');"
        "img.src='a\\0b.png';check(img.getAttribute('src')==='a\\0b.png' && img.src==='https://example.test/dir/a%00b.png','NUL not truncated');"
        "img.srcset='photo-\\ud800.png 1x';check(img.getAttribute('srcset')==='photo-\\ufffd.png 1x','USV setter');"
        "img.setAttribute('srcset','a\\udfff.png 1x');check(img.srcset==='a\\ufffd.png 1x','USV getter');"
        "img.src='\\ud800';check(img.getAttribute('src')==='\\ufffd','src USVString');"
        "img.sizes='\\ud800';check(img.getAttribute('sizes')==='\\ud800' && img.sizes==='\\ud800','sizes DOMString');"
        "img.src=null;img.srcset=undefined;img.sizes=null;"
        "check(img.getAttribute('src')==='null' && img.srcset==='undefined' && img.sizes==='null','IDL null conversion');"
        "let converted=false;const value={toString(){converted=true;return 'x'}};"
        "const src=Object.getOwnPropertyDescriptor(HTMLImageElement.prototype,'src');"
        "raises(()=>src.set.call(document.body,value));raises(()=>src.get.call({}));"
        "check(!converted && src.enumerable && src.configurable,'brand before conversion and descriptor');"
        "raises(()=>{img.src=Symbol()});"
        "for(const name of ['complete','naturalWidth','naturalHeight','currentSrc']) {"
        "const d=Object.getOwnPropertyDescriptor(HTMLImageElement.prototype,name);"
        "check(d.enumerable && d.configurable && !d.set,'read-only '+name);raises(()=>d.get.call(document.body));}"
        "img.removeAttribute('srcset');img.src='';check(img.complete,'empty src complete');"
        "img.srcset='';check(!img.complete,'empty srcset remains unavailable in inert document');"
        "check(!(document.getElementById('svg') instanceof HTMLImageElement),'foreign namespace');",
        "HTMLImageElement interfaces, reflection, USVString and inert state"));
}
END_TEST

START_TEST(test_quickjs_rectangles)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "function raises(fn){try{fn()}catch(e){if(e instanceof TypeError)return;throw e}throw Error('missing TypeError')}"
        "let r=new DOMRect(10,20,-3,-4);"
        "check(r instanceof DOMRect && r instanceof DOMRectReadOnly,'inheritance');"
        "check(r.x===10 && r.y===20 && r.width===-3 && r.height===-4 && r.top===16 && r.right===10 && r.bottom===20 && r.left===7,'negative extents');"
        "r.x=4;r.width=8;check(r.left===4 && r.right===12,'mutable values');"
        "check(Object.prototype.toString.call(r)==='[object DOMRect]','tag');"
        "let ro=new DOMRectReadOnly('1',2,3,4);ro.x=100;check(ro.x===1 && ro.right===4,'readonly values');"
        "check(Object.getPrototypeOf(DOMRect)===DOMRectReadOnly,'constructor inheritance');"
        "class R extends DOMRect{};check(new R(1).x===1 && new R() instanceof R,'subclass');"
        "let order=[];let dict={get height(){order.push('h');return 4},get width(){order.push('w');return 3},"
        "get x(){order.push('x');return 1},get y(){order.push('y');return 2}};"
        "let copy=DOMRect.fromRect(dict);check(order.join('')==='hwxy' && copy.x===1 && copy.height===4,'dictionary conversion');"
        "check(DOMRectReadOnly.fromRect(copy).right===4 && !(DOMRectReadOnly.fromRect(copy) instanceof DOMRect),'readonly factory');"
        "check(R.fromRect({}) instanceof DOMRect && !(R.fromRect({}) instanceof R),'factory uses interface intrinsic');"
        "check(new DOMRect(undefined).x===0 && DOMRect.fromRect(null).width===0,'defaults');"
        "let json=copy.toJSON();check(Object.keys(json).join(',')==='x,y,width,height,top,right,bottom,left' && json.bottom===6,'JSON');"
        "copy.x=NaN;check(Number.isNaN(copy.left) && Number.isNaN(copy.right),'NaN preserved');"
        "check(new DOMRect(Infinity,0,2,3).right===Infinity,'infinity');"
        "raises(()=>DOMRect());raises(()=>DOMRect.fromRect(2));raises(()=>new DOMRect(Symbol()));"
        "raises(()=>DOMRectReadOnly.prototype.toJSON.call({}));"
        "let setter=Object.getOwnPropertyDescriptor(DOMRect.prototype,'x').set, converted=false;"
        "raises(()=>setter.call(ro,{valueOf(){converted=true;return 5}}));check(!converted,'brand before conversion');"
        "let sentinel={};try{DOMRect.fromRect({get height(){throw sentinel}});throw Error('missing exception')}"
        "catch(e){check(e===sentinel,'getter exception preserved')}",
        "Geometry Interfaces value rectangles"));
}
END_TEST

static void add_lazy_image_tests(TCase *tc)
{
    tcase_add_test(tc, test_quickjs_rectangles);
    tcase_add_test(tc, test_quickjs_image_interface);
    tcase_add_test(tc, test_quickjs_real_lazysizes_initialization);
    tcase_add_test(tc, test_quickjs_class_queries);
    tcase_add_test(tc, test_quickjs_class_query_conversion);
    tcase_add_test(tc, test_quickjs_create_event);
    tcase_add_test(tc, test_quickjs_create_event_errors);
    tcase_add_loop_test(tc, test_quickjs_loader_foundation_retirement, 0, 4);
}
