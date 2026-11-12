/* HTML Image factory, dimensions and IDL image event handlers. Rendering and
 * actual asynchronous requests are covered by test_js_images.py. */
START_TEST(test_quickjs_image_factory)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "function raises(fn){try{fn()}catch(e){if(e instanceof TypeError)return;throw e}throw Error('missing TypeError')}"
        "const img=new Image(); check(img instanceof Image && img instanceof HTMLImageElement,'factory prototype');"
        "check(img.ownerDocument===document && img.nodeName==='IMG' && !img.isConnected,'native ownership');"
        "check(img.width===0 && img.height===0 && img.complete && !img.hasAttribute('width'),'empty factory');"
        "check(Image.prototype===HTMLImageElement.prototype && img.constructor===HTMLImageElement,'shared prototype');"
        "check(Image.name==='Image' && Image.length===0,'function name and arity');"
        "const d=Object.getOwnPropertyDescriptor(Image,'prototype'),g=Object.getOwnPropertyDescriptor(window,'Image');"
        "check(!d.writable && !d.configurable && !d.enumerable && g.writable && g.configurable && !g.enumerable,'descriptors');"
        "raises(()=>Image()); raises(()=>new HTMLImageElement()); raises(()=>new Image(Symbol())); raises(()=>new Image(1n));"
        "let sized=new Image('42.9',null);check(sized.width===42 && sized.height===0 && sized.getAttribute('height')==='0','argument conversion');"
        "sized=new Image(undefined,17);check(!sized.hasAttribute('width') && sized.height===17,'optional undefined is absent');"
        "sized=new Image(-1,4294967298);check(sized.getAttribute('width')==='4294967295' && sized.height===2,'factory unsigned conversion');"
        "class SubImage extends Image{} const sub=new SubImage(21,12);"
        "check(sub instanceof SubImage && sub.width===21 && sub.height===12,'subclass prototype');"
        "let order=[];const ctor=new Proxy(function(){},{get(t,k){if(k==='prototype'){order.push('prototype');return SubImage.prototype}return t[k]}});"
        "const obj=Reflect.construct(Image,[{valueOf(){order.push('width');return 7}},{valueOf(){order.push('height');return 8}}],ctor);"
        "check(order.join()==='width,height,prototype' && obj instanceof SubImage && obj.width===7,'conversion order');"
        "function Empty(){}Empty.prototype=null;check(Object.getPrototypeOf(Reflect.construct(Image,[],Empty))===Image.prototype,'intrinsic fallback');"
        "const NativeImage=Image, proto=Image.prototype;window.Image=function(){throw Error('page constructor')};"
        "window.HTMLImageElement=function(){throw Error('page interface')};"
        "check(Object.getPrototypeOf(new NativeImage())===proto,'intrinsic image interface');",
        "Image factory, Web IDL, inheritance and ownership"));
}
END_TEST

START_TEST(test_quickjs_image_dimensions)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "function raises(fn){try{fn()}catch(e){if(e instanceof TypeError)return;throw e}throw Error('missing TypeError')}"
        "const img=new Image();"
        "for(const name of ['width','height']){"
        "const d=Object.getOwnPropertyDescriptor(HTMLImageElement.prototype,name);"
        "check(d.enumerable && d.configurable && !!d.set,'dimension descriptor');"
        "let converted=false;raises(()=>d.set.call(document.body,{valueOf(){converted=true;return 2}}));"
        "raises(()=>d.get.call({}));check(!converted,'brand before conversion');"
        "for(const [input,expected] of [['42.9',42],[-1,0],[2147483648,0],[4294967297,1],[NaN,0],[Infinity,0],[undefined,0]]){"
        "img[name]=input;check(img[name]===expected && img.getAttribute(name)===String(expected),'ReflectSetter '+input);}"
        "raises(()=>img[name]=Symbol());raises(()=>img[name]=1n);"
        "for(const [input,expected] of [['09',9],['0x20',0],[' +12px',12],['-0',0],['-1',0],['\\u000b12',0],['\\u00a012',0],['12\\0x',12],['',0]]){"
        "img.setAttribute(name,input);check(img[name]===expected,'HTML integer parsing '+input);}"
        "img.removeAttribute(name);check(img[name]===0,'missing unavailable dimension');}"
        "img.width=7;img.height=9;const copy=img.cloneNode();"
        "check(copy.width===7 && copy.height===9,'clone reflects dimensions');"
        "const inert=document.cloneNode();inert.appendChild(copy);"
        "check(copy.width===7 && copy.height===9 && copy.ownerDocument===inert,'inert dimensions');",
        "Image reflected dimensions, brands and native integer parsing"));
}
END_TEST

START_TEST(test_quickjs_image_handlers)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "var img=new Image(), order=[], first=()=>order.push('first'), last=()=>order.push('last');"
        "check(img.onload===null && img.onerror===null,'handler defaults');"
        "img.addEventListener('load',first);img.onload=()=>order.push('old');img.addEventListener('load',last);"
        "var handler=function(e){check(this===img && e.currentTarget===img && e.target===img,'handler identity');order.push('new')};"
        "img.onload=handler;img.removeEventListener('load',handler);img.dispatchEvent(new Event('load'));"
        "check(order.join()==='first,new,last' && img.onload===handler,'replacement preserves independent listener order');"
        "order=[];img.onload=null;img.onload=handler;img.dispatchEvent(new Event('load'));"
        "check(order.join()==='first,last,new','reactivation moves to end');"
        "for(const value of [undefined,42,'code',Symbol()]){img.onload=value;check(img.onload===null,'nonobject null');}"
        "const object={get handleEvent(){throw Error('not an EventListener')}};img.onload=object;"
        "check(img.onload===object,'noncallable retained');img.dispatchEvent(new Event('load'));"
        "img.onerror=()=>false;check(!img.dispatchEvent(new Event('error',{cancelable:true})),'false cancels ordinary error event');"
        "img.onerror=()=>0;check(img.dispatchEvent(new Event('error',{cancelable:true})),'only false cancels');"
        "var calls=0;img.onload=function(){calls++;img.onload=()=>calls+=10};"
        "img.dispatchEvent(new Event('load'));img.dispatchEvent(new Event('load'));check(calls===11,'replace while invoking');"
        "img.onload=()=>{throw Error('expected handler exception')};img.addEventListener('load',()=>calls++);"
        "img.dispatchEvent(new Event('load'));check(calls===12,'exception does not abort later listener');"
        "img.onload=handler;check(img.cloneNode().onload===null,'clone does not copy IDL handler');"
        "var inert=document.cloneNode();inert.appendChild(img);"
        "order=[];img.dispatchEvent(new Event('load'));check(order.join()==='first,last,new','adoption preserves handler');"
        "var retained={img};img.onload=function(){if(this!==retained.img)throw Error('lost handler closure')};",
        "Image handler order, conversion, cancellation, exceptions and adoption"));
    JS_RunGC(dom_heap->rt);
    ck_assert(exec_dom_script(dom_thread, "img.dispatchEvent(new Event('load'));", "image handler GC"));
}
END_TEST

START_TEST(test_quickjs_image_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
                         JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)),
        1);
    JS_FreeValue(dom_thread->ctx, global);
    const char *operations[] = {"new Image({valueOf(){retire();return 1}})",
        "Reflect.construct(Image,[],new Proxy(function(){},{get(){retire();return Image.prototype}}))",
        "(()=>{const img=new Image();img.width={valueOf(){retire();return 1}}})()",
        "(()=>{const img=new Image();retire();return img.onload})()"};
    char source[512];
    snprintf(source, sizeof(source),
        "try{%s;throw Error('accepted retired document')}catch(e){"
        "if(e.name!=='InvalidStateError')throw e}",
        operations[_i]);
    ck_assert_msg(exec_dom_script(dom_thread, source, "Image retirement"), "variant %d", _i);
}
END_TEST

START_TEST(test_quickjs_image_handler_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
                         JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)),
        1);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_dom_script(dom_thread,
        "var img=new Image(),after=false;img.id='retiring-image';document.body.appendChild(img);"
        "img.onload=()=>retire();img.addEventListener('load',()=>after=true);",
        "image retirement setup"));
    dom_string *id = NULL;
    dom_element *img = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"retiring-image", 14, &id), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(dom_html.document, id, &img), DOM_NO_ERR);
    ck_assert_ptr_nonnull(img);
    js_fire_element_event(dom_thread, (dom_node *)img, corestring_dom_load);
    dom_node_unref(img);
    dom_string_unref(id);
    ck_assert(dom_thread->closed);
    global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue after = JS_GetPropertyStr(dom_thread->ctx, global, "after");
    ck_assert_int_eq(JS_ToBool(dom_thread->ctx, after), 0);
    JS_FreeValue(dom_thread->ctx, after);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_image_allocation_failure)
{
    ck_assert(exec_dom_script(dom_thread,
        "function allocateImage(){const img=new Image(7,9);let calls=0;"
        "img.onload=()=>calls++;img.onerror=()=>false;img.width=42;"
        "img.dispatchEvent(new Event('load'));"
        "return calls===1 && img.width===42 && img.height===9 &&"
        "!img.dispatchEvent(new Event('error',{cancelable:true}));}allocateImage();",
        "image allocation warmup"));
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue callback = JS_GetPropertyStr(dom_thread->ctx, global, "allocateImage");
    JS_FreeValue(dom_thread->ctx, global);
    bool failed = false, succeeded = false;
    for (size_t extra = 0; extra <= 32768; extra += 128) {
        JS_RunGC(dom_heap->rt);
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(dom_heap->rt, &usage);
        JS_SetMemoryLimit(dom_heap->rt, usage.malloc_size + extra);
        JSValue result = JS_Call(dom_thread->ctx, callback, JS_UNDEFINED, 0, NULL);
        JS_SetMemoryLimit(dom_heap->rt, 0);
        if (JS_IsException(result)) {
            failed = true;
            JS_FreeValue(dom_thread->ctx, JS_GetException(dom_thread->ctx));
        } else {
            succeeded = true;
            ck_assert(JS_ToBool(dom_thread->ctx, result));
        }
        JS_FreeValue(dom_thread->ctx, result);
        if (succeeded)
            break;
    }
    ck_assert(failed && succeeded);
    JS_FreeValue(dom_thread->ctx, callback);
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "allocateImage()", "image recovery after allocation failure");
}
END_TEST

static void add_image_factory_tests(TCase *tc)
{
    tcase_add_test(tc, test_quickjs_image_factory);
    tcase_add_test(tc, test_quickjs_image_allocation_failure);
    tcase_add_test(tc, test_quickjs_image_dimensions);
    tcase_add_test(tc, test_quickjs_image_handlers);
    tcase_add_loop_test(tc, test_quickjs_image_retirement, 0, 4);
    tcase_add_test(tc, test_quickjs_image_handler_retirement);
}
