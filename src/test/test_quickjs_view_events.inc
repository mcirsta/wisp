/* CSSOM View dispatch and HTML's Window event-handler IDL attributes. */
START_TEST(test_quickjs_view_handlers)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "check(onscroll===null && onresize===null,'defaults');"
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
    assert_predicate(dom_thread, "order.join()==='capture,document,window,capture,element,resize'",
        "document scroll bubbles, element scroll does not, resize targets Window");
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

static void add_view_event_tests(TCase *tc)
{
    tcase_add_test(tc, test_quickjs_view_handlers);
    tcase_add_test(tc, test_quickjs_native_view_events);
    tcase_add_test(tc, test_quickjs_view_handler_retirement);
}
