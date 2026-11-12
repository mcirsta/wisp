/* XHR host semantics; real HTTP and final pixels live in the Qt integration. */
START_TEST(test_quickjs_xhr_values)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,msg){if(!ok)throw Error(msg)}"
        "function raises(name,fn){try{fn()}catch(e){check(e.name===name,'expected '+name+', got '+e);return}throw Error('missing '+name)}"
        "var x=new XMLHttpRequest(), events=[];"
        "check(x instanceof XMLHttpRequestEventTarget && x instanceof EventTarget && x.constructor===XMLHttpRequest,'inheritance');check(Object.getPrototypeOf(XMLHttpRequestEventTarget)===EventTarget,'constructor inheritance');"
        "check(Object.prototype.toString.call(x)==='[object XMLHttpRequest]','tag');"
        "check(x.readyState===0 && x.status===0 && x.response==='' && x.responseText==='' && x.statusText==='' && x.responseURL==='','defaults');"
        "check(x.timeout===0 && x.withCredentials===false && x.responseType==='','defaults 2');"
        "check(x.onload===null && x.onreadystatechange===null && x.DONE===4,'handlers and constants');"
        "raises('TypeError',()=>XMLHttpRequest()); raises('TypeError',()=>new XMLHttpRequestEventTarget());"
        "raises('TypeError',()=>XMLHttpRequest.prototype.open.call({},'GET','https://example.test/'));"
        "raises('InvalidStateError',()=>x.send()); raises('TypeError',()=>x.open());"
        "raises('SyntaxError',()=>x.open('bad method','https://example.test/'));"
        "raises('SecurityError',()=>x.open('trace','https://example.test/'));"
        "raises('NotSupportedError',()=>x.open('GET','https://example.test/',false));"
        "raises('TypeError',()=>x.open('\u0100','https://example.test/'));"
        "x.onreadystatechange=function(e){check(this===x && e.target===x && e.isTrusted,'OPENED event');events.push(x.readyState)};"
        "x.open('get','https://example.test/a#hash');x.open('GET','https://example.test/b');"
        "check(events.join()==='1','OPENED once');"
        "var marker={},bad={toString(){throw marker}};"
        "try{x.open(bad,'https://example.test/')}catch(e){check(e===marker,'method conversion')}"
        "try{x.send(bad);throw Error('accepted throwing body')}catch(e){check(e===marker,'body conversion')}"
        "check(x.readyState===1,'failed open unchanged');"
        "raises('SyntaxError',()=>x.setRequestHeader('bad name','value'));"
        "raises('SyntaxError',()=>x.setRequestHeader('x','a\\r\\nb'));"
        "raises('TypeError',()=>x.setRequestHeader('x','\u0100'));"
        "x.responseType='text';check(x.responseType==='text','text response');"
        "raises('NotSupportedError',()=>x.responseType='arraybuffer');"
        "x.responseType='unknown';check(x.responseType==='text','unknown enum ignored');"
        "x.timeout=-1;check(x.timeout===4294967295,'unsigned timeout');raises('TypeError',()=>x.timeout=1n);"
        "x.timeout=0; x.abort();check(x.readyState===1,'unsent OPENED abort does not reset');"
        "x.onload=()=>events.push('handler');x.addEventListener('load',()=>events.push('listener'));"
        "x.onload=()=>events.push('replacement');x.dispatchEvent(new ProgressEvent('load'));"
        "check(events.join()==='1,replacement,listener','shared handler order');"
        "class Derived extends XMLHttpRequest{};check(new Derived() instanceof Derived,'subclass prototype');",
        "XHR values, conversion, handlers and state validation"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_progress_event)
{
    ck_assert(exec_dom_script(dom_thread,
        "var e=new ProgressEvent('progress',{lengthComputable:true,loaded:4.8,total:12});"
        "if(!(e instanceof Event)||e instanceof UIEvent||e.isTrusted||e.loaded!==4||e.total!==12||!e.lengthComputable)throw Error('progress values');"
        "var defaults=new ProgressEvent('load');if(defaults.loaded||defaults.total||defaults.lengthComputable)throw Error('defaults');"
        "var order=[];new ProgressEvent('x',{get bubbles(){order.push('b')},get cancelable(){order.push('c')},"
        "get composed(){order.push('o')},get lengthComputable(){order.push('l')},get loaded(){order.push('n')},get total(){order.push('t')}});"
        "if(order.join()!=='b,c,o,l,n,t')throw Error('dictionary order '+order);"
        "try{new ProgressEvent('x',{loaded:1n});throw Error('accepted bigint')}catch(e){if(!(e instanceof TypeError))throw e;}"
        "var get=Object.getOwnPropertyDescriptor(ProgressEvent.prototype,'loaded').get;"
        "try{get.call(new Event('x'));throw Error('accepted base event')}catch(e){if(!(e instanceof TypeError))throw e;}",
        "ProgressEvent inheritance, conversion and brands"));
}
END_TEST

START_TEST(test_quickjs_xhr_tasks)
{
    setup_timers();
    ck_assert(exec_dom_script(dom_thread,
        "var x=new XMLHttpRequest(), seen=[];x.open('GET','https://example.test/');"
        "x.onloadstart=()=>seen.push('start');x.onerror=()=>seen.push('error');x.onloadend=()=>seen.push('end');"
        "x.send();if(x.readyState!==1||seen.join()!=='start')throw Error('send was synchronous');"
        "try{x.send();throw Error('duplicate send accepted')}catch(e){if(e.name!=='InvalidStateError')throw e;}",
        "XHR queues native work"));
    /* No real document origin in this fixture: fail asynchronously, without
     * starting a network fetch. The loopback test separately exercises curl. */
    JS_RunGC(dom_heap->rt);
    drain_timers(4);
    assert_predicate(
        dom_thread, "x.readyState===4 && x.status===0 && seen.join()==='start,error,end'", "network error events");
    ck_assert(exec_dom_script(dom_thread,
        "x.open('GET','https://example.test/');x.onloadstart=()=>x.abort();seen=[];x.send();"
        "if(x.readyState!==0||seen.join()!=='end')throw Error('loadstart abort');",
        "synchronous loadstart abort removes scheduled fetch"));
    drain_timers(4);
    ck_assert(exec_dom_script(
        dom_thread, "x.onloadstart=null;x.open('GET','https://example.test/');x.send();", "XHR document retirement"));
    js_closethread(dom_thread);
    JS_RunGC(dom_heap->rt);
    teardown_timers();
}
END_TEST

START_TEST(test_quickjs_xhr_conversion_retirement)
{
    static const char *calls[] = {"x.open({toString(){retire();return 'GET'}},'https://example.test/')",
        "x.open('GET',{toString(){retire();return 'https://example.test/'}})",
        "x.setRequestHeader('X-Probe',{toString(){retire();return 'v'}})",
        "x.getResponseHeader({toString(){retire();return 'x'}})",
        "x.overrideMimeType({toString(){retire();return 'text/plain'}})", "x.timeout={valueOf(){retire();return 5}}",
        "x.responseType={toString(){retire();return 'text'}}",
        "Reflect.construct(XMLHttpRequest,[],new Proxy(function(){},{get(){retire();return XMLHttpRequest.prototype}}))",
        "new ProgressEvent('x',{get loaded(){retire();return 1}})", "x.send({toString(){retire();return 'body'}})"};
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(
        dom_thread->ctx, global, "retire", JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0));
    JS_FreeValue(dom_thread->ctx, global);
    char script[1024];
    snprintf(script, sizeof(script),
        "var x=new XMLHttpRequest();x.open('GET','https://example.test/');"
        "try{%s;throw Error('accepted retired object')}catch(e){if(e.name!=='InvalidStateError')throw e;}",
        calls[_i]);
    ck_assert_msg(exec_dom_script(dom_thread, script, "XHR conversion retirement"), "Retirement case %d", _i);
}
END_TEST

START_TEST(test_quickjs_xhr_gc)
{
    JSMemoryUsage before, after;
    for (unsigned i = 0; i < 4; i++) {
        ck_assert(exec_dom_script(dom_thread,
            "(()=>{for(let i=0;i<100;i++){let x=new XMLHttpRequest();x.onload=()=>x;x.saved=new ProgressEvent('load');"
            "x.saved.owner=x;x.addEventListener('load',()=>x);}})();",
            "XHR unreachable cycles"));
        JS_RunGC(dom_heap->rt);
        JS_ComputeMemoryUsage(dom_heap->rt, &after);
        if (i == 0)
            before = after;
        else
            ck_assert_msg(after.obj_count <= before.obj_count + 2, "XHR cycles leaked %lld objects",
                (long long)(after.obj_count - before.obj_count));
    }
    ck_assert_ptr_null(dom_thread->requests);
}
END_TEST

START_TEST(test_quickjs_xhr_scheduler_failure)
{
    setup_timers();
    ck_assert(
        exec_script(dom_thread, "var x=new XMLHttpRequest();x.open('GET','https://example.test/');", "XHR setup"));
    fail_schedule = true;
    ck_assert(!exec_script(dom_thread, "x.send();", "XHR scheduling failure"));
    fail_schedule = false;
    ck_assert(exec_script(dom_thread, "x.send();", "XHR scheduling recovery"));
    preserve_failed_schedule = true;
    fail_schedule = true;
    ck_assert(!exec_script(dom_thread, "x.timeout=10;", "XHR timeout scheduling failure"));
    fail_schedule = false;
    teardown_timers();
}
END_TEST

START_TEST(test_quickjs_xhr_execution_limit)
{
    setup_timers();
    ck_assert(exec_script(dom_thread,
        "var x=new XMLHttpRequest(),ended=false;x.open('GET','https://example.test/');"
        "x.onerror=()=>{while(true){}};x.onloadend=()=>ended=true;x.send();",
        "XHR callback budget"));
    drain_timers(4);
    assert_predicate(
        dom_thread, "!ended && x.readyState===4", "interrupted XHR stops event sequence and releases work");
    teardown_timers();
}
END_TEST

static void add_xhr_tests(TCase *test)
{
    tcase_add_test(test, test_quickjs_xhr_values);
    tcase_add_test(test, test_quickjs_progress_event);
    tcase_add_test(test, test_quickjs_xhr_tasks);
    tcase_add_test(test, test_quickjs_xhr_gc);
    tcase_add_test(test, test_quickjs_xhr_scheduler_failure);
    tcase_add_test(test, test_quickjs_xhr_execution_limit);
    tcase_add_loop_test(test, test_quickjs_xhr_conversion_retirement, 0, 10);
}
