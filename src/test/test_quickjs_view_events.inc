/* CSSOM View dispatch and HTML's Window event-handler IDL attributes. */
START_TEST(test_quickjs_view_handlers)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "check(onscroll===null && onresize===null && onscrollend===null,'defaults');"
        "var order=[],a=()=>order.push('a'),b=()=>order.push('b'),first=()=>order.push('first');"
        "addEventListener('scroll',a);onscroll=first;addEventListener('scroll',b);"
        "var second=function(e){check(this===window && e.currentTarget===window,'handler receiver');order.push('second')};"
        "onscroll=second;removeEventListener('scroll',second);dispatchEvent(new Event('scroll'));"
        "check(order.join()==='a,second,b' && onscroll===second,'replacement keeps independent registration position');"
        "order=[];onscroll=null;onscroll=first;dispatchEvent(new Event('scroll'));"
        "check(order.join()==='a,b,first','reactivation gets a new position');"
        "removeEventListener('scroll',a);removeEventListener('scroll',b);"
        "for(const value of [42,'code',Symbol(),undefined]){onscroll=value;check(onscroll===null,'non-object conversion')}"
        "var object={get handleEvent(){throw Error('not an EventListener')}};onscroll=object;"
        "check(onscroll===object,'non-callable object retained');dispatchEvent(new Event('scroll'));"
        "onscroll=()=>false;check(!dispatchEvent(new Event('scroll',{cancelable:true})),'false cancels');"
        "onscroll=()=>0;check(dispatchEvent(new Event('scroll',{cancelable:true})),'only boolean false cancels');"
        "var calls=0;onscroll=function(){calls++;onscroll=()=>calls+=10};dispatchEvent(new Event('scroll'));"
        "dispatchEvent(new Event('scroll'));check(calls===11,'replace during invocation');"
        "const get=Object.getOwnPropertyDescriptor(window,'onscroll').get;let bad=false;"
        "try{get.call({})}catch(e){bad=e instanceof TypeError}check(bad,'Window receiver');"
        "var cycle={window};onresize=function(){check(cycle.window===window,'retained closure')};",
        "Window event-handler identity, order, conversion and cancellation"));
    JS_RunGC(dom_heap->rt);
    ck_assert(exec_dom_script(dom_thread, "dispatchEvent(new Event('resize'));", "handler GC"));
}
END_TEST

START_TEST(test_quickjs_native_view_events)
{
    ck_assert(exec_dom_script(dom_thread,
        "var order=[];function verify(e,target,bubbles){if(!e.isTrusted || e.cancelable || e.composed ||"
        "e.bubbles!==bubbles || e.target!==target)throw Error('native view event flags')}"
        "window.addEventListener('scroll',e=>{order.push('capture');verify(e,e.target,e.target===document)},true);"
        "document.addEventListener('scroll',e=>{verify(e,document,true);order.push('document')});"
        "onscroll=e=>{verify(e,document,true);order.push('window')};"
        "document.addEventListener('scrollend',e=>{verify(e,document,true);order.push('document-end')});"
        "onscrollend=e=>{verify(e,document,true);order.push('window-end')};"
        "document.getElementById('target').addEventListener('scroll',e=>{"
        "verify(e,document.getElementById('target'),false);order.push('element')});"
        "onresize=e=>{verify(e,window,false);if(e.eventPhase!==2)throw Error('window target phase');order.push('resize')};",
        "native view listener setup"));
    ck_assert(js_fire_element_event(dom_thread, (dom_node *)dom_html.document, corestring_dom_scroll));
    dom_string *id;
    dom_element *element;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"target", 6, &id), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(dom_html.document, id, &element), DOM_NO_ERR);
    ck_assert(js_fire_element_event(dom_thread, (dom_node *)element, corestring_dom_scroll));
    dom_node_unref(element);
    dom_string_unref(id);
    ck_assert(js_fire_window_event(dom_thread, corestring_dom_resize));
    ck_assert(js_fire_element_event(dom_thread, (dom_node *)dom_html.document, corestring_dom_scrollend));
    assert_predicate(dom_thread, "order.join()==='capture,document,window,capture,element,resize,document-end,window-end'",
        "document scroll/scrollend bubble, element scroll does not, resize targets Window");
}
END_TEST

START_TEST(test_quickjs_view_handler_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
        JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)), 1);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_dom_script(dom_thread,
        "var after=false;onscroll=()=>retire();addEventListener('scroll',()=>after=true);",
        "view retirement setup"));
    js_fire_element_event(dom_thread, (dom_node *)dom_html.document, corestring_dom_scroll);
    ck_assert(dom_thread->closed);
    global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue value = JS_GetPropertyStr(dom_thread->ctx, global, "after");
    ck_assert_int_eq(JS_ToBool(dom_thread->ctx, value), 0);
    JS_FreeValue(dom_thread->ctx, value);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_scroll_conversion)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "function typeError(fn){try{fn()}catch(e){check(e instanceof TypeError,'exception type');return}throw Error('no TypeError')}"
        "for(const name of ['scroll','scrollTo','scrollBy']){"
        " const fn=window[name],d=Object.getOwnPropertyDescriptor(window,name);"
        " check(typeof fn==='function' && fn.length===0 && d.writable && d.enumerable && d.configurable,'descriptor');"
        " check(fn()===undefined && fn(null)===undefined && fn(undefined)===undefined,'no viewport');"
        " for(const value of [1,true,'1',Symbol(),1n])typeError(()=>fn(value));"
        " for(const value of [NaN,Infinity,-Infinity,'42',null])check(fn(value,value)===undefined,'numeric conversion');"
        " typeError(()=>fn(Symbol(),0));typeError(()=>fn(0,1n));typeError(()=>fn.call({},0,0));"
        " check(fn.call(null,0,0)===undefined,'nullable receiver');"
        " for(const behavior of ['auto','instant','smooth',undefined])fn({behavior,left:4,top:8});"
        " for(const behavior of ['',null,'SMOOTH','auto\\0bad',Symbol()])typeError(()=>fn({behavior}));"
        " let order=[];fn({get behavior(){order.push('b');return {toString(){order.push('bs');return 'auto'}}},"
        " get left(){order.push('l');return {valueOf(){order.push('lv');return 3}}},"
        " get top(){order.push('t');return {valueOf(){order.push('tv');return 4}}}});"
        " check(order.join()==='b,bs,l,lv,t,tv','dictionary order');"
        " order=[];fn({valueOf(){order.push('x');return 1}},{valueOf(){order.push('y');return 2}},Symbol());"
        " check(order.join()==='x,y','numeric overload and ignored extra argument');"
        " const sentinel={};let caught=false;try{fn({get left(){throw sentinel},get top(){throw Error('late read')}})}"
        " catch(e){caught=e===sentinel}check(caught,'getter exception preserved');"
        " caught=false;try{fn({behavior:{toString(){throw sentinel}}})}catch(e){caught=e===sentinel}"
        " check(caught,'behavior conversion exception preserved');"
        " fn(Object.create({left:1,top:2}));"
        "}", "Window scroll overloads, conversion order and errors without a viewport"));
}
END_TEST

START_TEST(test_quickjs_scroll_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
        JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)), 1);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_dom_script(dom_thread,
        "var converted=false;scrollTo({get left(){retire();return 20},get top(){converted=true;return 30}});",
        "scroll conversion may retire the document"));
    ck_assert(dom_thread->closed);
    global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue value = JS_GetPropertyStr(dom_thread->ctx, global, "converted");
    ck_assert_int_eq(JS_ToBool(dom_thread->ctx, value), 1);
    JS_FreeValue(dom_thread->ctx, value);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_document_compat_mode)
{
    dom_document_quirks_mode modes[] = {
        DOM_DOCUMENT_QUIRKS_MODE_NONE, DOM_DOCUMENT_QUIRKS_MODE_LIMITED, DOM_DOCUMENT_QUIRKS_MODE_FULL};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(*modes); i++) {
        ck_assert_int_eq(dom_document_set_quirks_mode(dom_html.document, modes[i]), DOM_NO_ERR);
        ck_assert(exec_dom_script(dom_thread,
            "function check(ok,label){if(!ok)throw Error(label)}"
            "var expected=document.compatMode;"
            "check(document.cloneNode().compatMode===expected && document.cloneNode(true).compatMode===expected,'clone mode');"
            "const d=Object.getOwnPropertyDescriptor(Document.prototype,'compatMode');"
            "check(typeof d.get==='function' && d.set===undefined && d.enumerable && d.configurable,'readonly descriptor');"
            "let bad=false;try{d.get.call(document.body)}catch(e){bad=e instanceof TypeError}check(bad,'Document brand');"
            "document.compatMode='other';check(document.compatMode===expected,'assignment cannot replace mode');",
            "Document mode belongs to each native document, including inert clones"));
        assert_predicate(dom_thread, modes[i] == DOM_DOCUMENT_QUIRKS_MODE_FULL
            ? "document.compatMode==='BackCompat'" : "document.compatMode==='CSS1Compat'", "compatMode mapping");
    }
}
END_TEST

static void add_view_event_tests(TCase *tc)
{
    tcase_add_test(tc, test_quickjs_view_handlers);
    tcase_add_test(tc, test_quickjs_native_view_events);
    tcase_add_test(tc, test_quickjs_view_handler_retirement);
    tcase_add_test(tc, test_quickjs_scroll_conversion);
    tcase_add_test(tc, test_quickjs_scroll_retirement);
    tcase_add_test(tc, test_quickjs_document_compat_mode);
}
