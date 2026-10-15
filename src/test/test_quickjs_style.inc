/* CSSOM inline styles use the real DOM fixture and its retirement/GC harness. */
START_TEST(test_quickjs_style_values)
{
    ck_assert(exec_dom_script(dom_thread,
        "var e=document.getElementById('target'), s=e.style;"
        "function check(ok, label){if(!ok)throw Error(label);}"
        "check(s===e.style && s instanceof CSSStyleProperties && s instanceof CSSStyleDeclaration,'identity');"
        "check(s.parentRule===null && s.length===0 && s.cssText==='' && e.getAttribute('style')===null,'empty read');"
        "check(Object.getPrototypeOf(CSSStyleProperties)===CSSStyleDeclaration,'constructor inheritance');"
        "check(Object.prototype.toString.call(s)==='[object CSSStyleProperties]','tag');"
        "s.opacity='0.5'; check(s.opacity==='0.5' && e.getAttribute('style')==='opacity: 0.5;','opacity write');"
        "s.backgroundColor='red'; check(s['background-color']==='red','dashed alias');"
        "s.cssFloat='left'; check(s.float==='left' && s.getPropertyValue('float')==='left','float alias');"
        "s.setProperty('WIDTH','20px','IMPORTANT'); check(s.width==='20px' && s.getPropertyPriority('width')==='important','priority');"
        "s.setProperty('--Case','4px'); s.setProperty('--case','6px');"
        "check(s.getPropertyValue('--Case')==='4px' && s.getPropertyValue('--case')==='6px','custom case');"
        "const before=s.cssText; s.width='garbage'; s.setProperty('height','10px','!important');"
        "s.setProperty('unknown-property','x'); check(s.cssText===before,'invalid noops');"
        "s.width='30px'; check(s.getPropertyPriority('width')==='' && s.width==='30px','setter replaces priority');"
        "check(s.removeProperty('width')==='30px' && s.width==='' && s.removeProperty('absent')==='','remove readback');"
        "s.cssText='margin: var(--m); color: red;'; s.marginTop='8px';"
        "check(s.margin==='' && s.marginTop==='8px' && s.length===5,'partial pending shorthand');"
        "check(s.cssText==='margin-top: 8px; margin-right: ; margin-bottom: ; margin-left: ; color: red;','pending members are not serializable');"
        "s.opacity='0.7'; check(s.length===6,'unrelated edit preserves pending members');"
        "e.setAttribute('style',e.getAttribute('style'));"
        "check(s.length===3 && s.marginRight==='','identical attribute assignment replaces state');"
        "e.style='height: 22px'; check(e.style===s && s.height==='22px' && s.length===1,'PutForwards');"
        "s.height=null; check(s.length===0 && e.getAttribute('style')==='','null removes');"
        "s.height=undefined; check(s.length===0,'undefined is parsed, not null');"
        "s.cssText='--x: undefined'; s.setProperty('--x',undefined); check(s.getPropertyValue('--x')==='undefined','undefined value');"
        "s.cssText=null; check(s.length===0,'cssText normal string conversion');"
        "const svg=document.getElementById('svg'); svg.style.opacity='0.25';"
        "check(svg instanceof SVGElement && svg.style.opacity==='0.25','SVG inline style');",
        "CSSOM values, shorthands and synchronization"));
}
END_TEST

START_TEST(test_quickjs_style_indexed)
{
    ck_assert(exec_dom_script(dom_thread,
        "var s=document.body.style; s.cssText='width:1px;height:2px';"
        "const sym=Symbol(); s.note=42; s[sym]='symbol';"
        "if([...s].join()!=='width,height'||s.item(-1)!==''||s.item(4294967296)!=='width')throw Error('indices');"
        "const k=Reflect.ownKeys(s); if(k.length!==4||k[0]!=='0'||k[1]!=='1'||k[2]!=='note'||k[3]!==sym)throw Error('keys');"
        "const d=Object.getOwnPropertyDescriptor(s,'0');"
        "if(!d.enumerable||!d.configurable||d.writable||d.value!=='width')throw Error('descriptor');"
        "for(const key of ['0','99']){if(Reflect.set(s,key,'wrong')||Reflect.defineProperty(s,key,{value:'wrong'}))throw Error('write index');}"
        "if(Reflect.deleteProperty(s,'0')||!Reflect.deleteProperty(s,'99')||Reflect.preventExtensions(s))throw Error('delete/extensibility');"
        "s.removeProperty('width'); if(s[0]!=='height'||s[1]!==undefined||'1' in s)throw Error('live index');"
        "s['01']='ordinary'; if(s['01']!=='ordinary'||s.note!==42||s[sym]!=='symbol')throw Error('expandos');"
        "s.setProperty=()=>{throw Error('overridden method used');}; s.height='3px';"
        "if(s.height!=='3px')throw Error('internal method');"
        "var e=document.getElementById('target'), forwarded;"
        "Object.defineProperty(e,'style',{get(){return {set cssText(v){forwarded=v;}}}});"
        "const setter=Object.getOwnPropertyDescriptor(HTMLElement.prototype,'style').set; const value={}; setter.call(e,value);"
        "if(forwarded!==value)throw Error('ordinary PutForwards');",
        "CSSOM live indices, expandos and internal/forwarded operations"));
}
END_TEST

START_TEST(test_quickjs_style_conversion)
{
    ck_assert(exec_dom_script(dom_thread,
        "var e=document.getElementById('target'), s=e.style, order=[], sentinel={};"
        "function raises(f){let caught=false;try{f()}catch(e){caught=e instanceof TypeError;}if(!caught)throw Error('missing TypeError');}"
        "for(const C of [CSSStyleDeclaration,CSSStyleProperties])raises(()=>new C());"
        "for(const n of ['item','getPropertyValue','getPropertyPriority','removeProperty','setProperty'])raises(()=>s[n]());"
        "raises(()=>s.setProperty('color')); raises(()=>s.setProperty(Symbol(),'red')); raises(()=>s.item(1n));"
        "raises(()=>s.setProperty.call({}, {toString(){throw Error('brand must precede conversion')}},'red'));"
        "raises(()=>Object.getOwnPropertyDescriptor(HTMLElement.prototype,'style').get.call(document));"
        "raises(()=>Object.getOwnPropertyDescriptor(HTMLElement.prototype,'style').get.call(document.getElementById('svg')));"
        "raises(()=>Object.getOwnPropertyDescriptor(SVGElement.prototype,'style').get.call(e));"
        "try{s.setProperty('unknown',{toString(){order.push('value');return 'red'}},{toString(){throw sentinel}})}"
        "catch(e){if(e!==sentinel)throw e} if(order.join()!=='value'||s.length!==0)throw Error('conversion order');"
        "s.setProperty('width',{toString(){e.setAttribute('style','height: 5px');return '10px'}},undefined);"
        "if(s.height!=='5px'||s.width!=='10px')throw Error('stale state after conversion');"
        "s.setProperty('width',null,'bad-priority'); if(s.width!=='')throw Error('empty before priority validation');"
        "s.cssText={toString(){s.color='red';return 'height: 8px'}};"
        "if(s.color!==''||s.height!=='8px')throw Error('replacement after conversion');",
        "CSSOM argument conversion and receiver checks"));
}
END_TEST

START_TEST(test_quickjs_style_native_assignment)
{
    ck_assert(exec_dom_script(dom_thread,
        "var e=document.getElementById('target'), s=e.style;"
        "function pending(){s.cssText='margin:var(--m);color:red'; s.marginTop='8px';} pending();",
        "native style setup"));
    dom_node *element = NULL;
    dom_string *id = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"target", 6, &id), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(dom_html.document, id, &element), DOM_NO_ERR);
    dom_string_unref(id);
    dom_attr *attr = NULL;
    ck_assert_int_eq(dom_element_get_attribute_node(element, corestring_dom_style, &attr), DOM_NO_ERR);
    for (unsigned variant = 0; variant < 5; variant++) {
        ck_assert(exec_dom_script(dom_thread, "pending()", "restore pending style"));
        dom_string *text = NULL;
        ck_assert_int_eq(dom_attr_get_value(attr, &text), DOM_NO_ERR);
        if (variant == 0)
            ck_assert_int_eq(dom_attr_set_value(attr, text), DOM_NO_ERR);
        else if (variant == 1)
            ck_assert_int_eq(dom_node_set_node_value(attr, text), DOM_NO_ERR);
        else if (variant == 2)
            ck_assert_int_eq(dom_node_set_text_content(attr, text), DOM_NO_ERR);
        else if (variant == 3) {
            dom_attr *removed = NULL, *replaced = NULL;
            ck_assert_int_eq(dom_element_remove_attribute_node(element, attr, &removed), DOM_NO_ERR);
            ck_assert_int_eq(dom_element_set_attribute_node(element, attr, &replaced), DOM_NO_ERR);
            dom_node_unref(removed);
            dom_node_unref(replaced);
        } else {
            dom_node *copy = NULL;
            dom_attr *replaced = NULL;
            ck_assert_int_eq(dom_node_clone_node(attr, true, &copy), DOM_NO_ERR);
            ck_assert_int_eq(dom_element_set_attribute_node(element, copy, &replaced), DOM_NO_ERR);
            dom_node_unref(replaced);
            dom_node_unref(copy);
        }
        dom_string_unref(text);
        assert_predicate(dom_thread, "s.length===2 && s.marginTop==='8px' && s.color==='red'",
            "native assignment discards pending state");
    }
    dom_node_unref(attr);
    dom_node_unref(element);
    ck_assert(exec_dom_script(dom_thread,
        "e.removeAttribute('style'); if(s.length!==0 || s.cssText!=='')throw Error('remove attr');",
        "removed and cloned attributes"));
}
END_TEST

START_TEST(test_quickjs_style_retirement)
{
    ck_assert(exec_dom_script(dom_thread,
        "var e=document.createElement('div'), s=e.style; s.cycle={e,s}; s.opacity='0.4'; e=null;", "detached style"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "s.opacity==='0.4' && s.cycle.e.style===s", "style keeps detached element alive");
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue style = JS_GetPropertyStr(dom_thread->ctx, global, "s");
    js_closethread(dom_thread);
    JSValue value = JS_GetPropertyStr(dom_thread->ctx, style, "cssText");
    ck_assert(JS_IsException(value));
    JS_FreeValue(dom_thread->ctx, JS_GetException(dom_thread->ctx));
    JS_FreeValue(dom_thread->ctx, style);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_style_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
                         JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)),
        1);
    JS_FreeValue(dom_thread->ctx, global);
    const char *operations[] = {"s.width=value", "s.cssText=value", "s.setProperty(value,'2px')",
        "s.setProperty('width',value)", "s.setProperty('width','2px',value)", "s.removeProperty(value)",
        "s.getPropertyValue(value)", "s.getPropertyPriority(value)", "s.item({valueOf(){retire();return 0}})"};
    char script[600];
    snprintf(script, sizeof(script),
        "var s=document.body.style, value={toString(){retire();return 'width'}};"
        "let caught=false; try{%s}catch(e){caught=e.name==='InvalidStateError'}"
        "if(!caught)throw Error('retired conversion');",
        operations[_i]);
    ck_assert_msg(exec_dom_script(dom_thread, script, "style conversion retirement"), "operation %s", operations[_i]);
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

START_TEST(test_quickjs_computed_style_contract)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "function raises(fn,name){try{fn()}catch(e){if(e.name===name)return;throw e}throw Error('missing '+name)}"
        "var e=document.createElement('div'),s=getComputedStyle(e);"
        "check(s instanceof CSSStyleProperties && s instanceof CSSStyleDeclaration,'prototype');"
        "check(s!==getComputedStyle(e) && s!==e.style,'fresh readonly object');"
        "check(s.length===0 && s.cssText==='' && s.parentRule===null && s.opacity==='','detached');"
        "check(s.getPropertyPriority('opacity')==='' && s.item(0)==='' && s[0]===undefined,'empty declaration');"
        "for(const fn of [()=>s.opacity='.5',()=>s.cssText='',()=>s.removeProperty('missing'),"
        "()=>s.setProperty('unknown','','bad')])raises(fn,'NoModificationAllowedError');"
        "let order=[];const arg=(text)=>({toString(){order.push(text);return text}});"
        "raises(()=>s.setProperty(arg('opacity'),arg('.5'),arg('important')),'NoModificationAllowedError');"
        "check(order.join()==='opacity,.5,important','convert all args before readonly check');"
        "raises(()=>s.setProperty('opacity',Symbol()),'TypeError');raises(()=>s.cssText=Symbol(),'TypeError');"
        "raises(()=>getComputedStyle(),'TypeError');raises(()=>getComputedStyle(document),'TypeError');"
        "raises(()=>getComputedStyle.call({},e),'TypeError');raises(()=>getComputedStyle(e,Symbol()),'TypeError');"
        "check(getComputedStyle.call(null,e).length===0,'Window default receiver');"
        "const marker={};let caught=false;try{getComputedStyle(e,{toString(){throw marker}})}catch(x){caught=x===marker}"
        "check(caught,'pseudo conversion exception');"
        "s.note=42;s.cycle={s,e};e=null;check(s.note===42,'expando');",
        "computed CSSOM interface and read-only conversion"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "s.cycle.s===s && s.cycle.e instanceof Element && s.length===0",
        "computed wrapper retains its element");
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue style = JS_GetPropertyStr(dom_thread->ctx, global, "s");
    js_closethread(dom_thread);
    JSValue value = JS_GetPropertyStr(dom_thread->ctx, style, "opacity");
    ck_assert(JS_IsException(value));
    JS_FreeValue(dom_thread->ctx, JS_GetException(dom_thread->ctx));
    JS_FreeValue(dom_thread->ctx, style);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_computed_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
        JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)), 1);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_dom_script(dom_thread,
        "let caught=false;try{getComputedStyle(document.body,{toString(){retire();return '::before'}})}"
        "catch(e){caught=e.name==='InvalidStateError'}if(!caught)throw Error('retired pseudo conversion');",
        "computed pseudo conversion retirement"));
}
END_TEST

static void add_style_tests(TCase *tc)
{
    tcase_add_test(tc, test_quickjs_style_values);
    tcase_add_test(tc, test_quickjs_style_indexed);
    tcase_add_test(tc, test_quickjs_style_conversion);
    tcase_add_test(tc, test_quickjs_style_native_assignment);
    tcase_add_test(tc, test_quickjs_style_retirement);
    tcase_add_loop_test(tc, test_quickjs_style_conversion_retirement, 0, 9);
    tcase_add_test(tc, test_quickjs_computed_style_contract);
    tcase_add_test(tc, test_quickjs_computed_conversion_retirement);
}
