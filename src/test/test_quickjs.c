/*
 * Copyright 2024 Neosurf Contributors
 *
 * This file is part of NeoSurf.
 *
 * NeoSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/**
 * \file
 * Unit tests for QuickJS-ng JavaScript engine integration.
 */

#include <check.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "content/handlers/javascript/js.h"

/* Include QuickJS directly for console binding tests */
#include "content/handlers/javascript/quickjs/console.h"
#include "quickjs.h"

/**
 * Test that js_initialise and js_finalise work without crashing.
 */
START_TEST(test_quickjs_init_finalise)
{
    js_initialise();
    js_finalise();
}
END_TEST

static int cancelled_job_objects_freed;
static int executed_job_order;

static void cancelled_job_finalizer(JSRuntime *rt, JSValueConst value)
{
    (void)rt;
    (void)value;
    cancelled_job_objects_freed++;
}

static JSValue record_job(JSContext *ctx, int argc, JSValueConst *argv)
{
    int32_t value;
    ck_assert_int_eq(argc, 1);
    ck_assert_int_eq(JS_ToInt32(ctx, &value, argv[0]), 0);
    executed_job_order = executed_job_order * 10 + value;
    return JS_UNDEFINED;
}

START_TEST(test_quickjs_cancel_jobs)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *retired = JS_NewContext(rt);
    JSContext *active = JS_NewContext(rt);
    JSContext *executed = NULL;
    JSClassID class_id = 0;
    JSClassDef class_def = {.class_name = "QueuedObject", .finalizer = cancelled_job_finalizer};
    JSValue object, value;

    cancelled_job_objects_freed = executed_job_order = 0;
    JS_NewClassID(rt, &class_id);
    ck_assert_int_eq(JS_NewClass(rt, class_id, &class_def), 0);
    object = JS_NewObjectClass(retired, class_id);
    ck_assert(!JS_IsException(object));
    ck_assert_int_eq(JS_EnqueueJob(retired, record_job, 1, &object), 0);
    value = JS_NewInt32(active, 1);
    ck_assert_int_eq(JS_EnqueueJob(active, record_job, 1, &value), 0);
    ck_assert_int_eq(JS_EnqueueJob(retired, record_job, 1, &object), 0);
    value = JS_NewInt32(active, 2);
    ck_assert_int_eq(JS_EnqueueJob(active, record_job, 1, &value), 0);
    JS_FreeValue(retired, object);
    ck_assert_int_eq(cancelled_job_objects_freed, 0);

    JS_CloseContextJobs(retired);
    JS_CloseContextJobs(retired); /* Closing twice must be harmless. */
    ck_assert_int_eq(executed_job_order, 0);
    ck_assert_int_eq(cancelled_job_objects_freed, 1);
    object = JS_NewObjectClass(retired, class_id);
    ck_assert(!JS_IsException(object));
    ck_assert_int_eq(JS_EnqueueJob(retired, record_job, 1, &object), 0);
    JS_FreeValue(retired, object);
    ck_assert_msg(cancelled_job_objects_freed == 2, "A closed context retained a new job's argument");
    JS_FreeContext(retired);
    JS_RunGC(rt);

    for (int i = 0; i < 2; i++) {
        ck_assert_int_eq(JS_ExecutePendingJob(rt, &executed), 1);
        ck_assert_ptr_eq(executed, active);
    }
    ck_assert_int_eq(executed_job_order, 12);
    ck_assert(!JS_IsJobPending(rt));
    JS_CloseContextJobs(active);
    JS_FreeContext(active);
    JS_FreeRuntime(rt);
}
END_TEST

START_TEST(test_quickjs_cancel_promise_jobs)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *retired = JS_NewContext(rt);
    JSContext *active = JS_NewContext(rt);
    JSContext *executed = NULL;
    const char *code = "var order = 0;"
                       "Promise.resolve().then(function() { order = order * 10 + 1; });"
                       "Promise.resolve().then(function() { order = order * 10 + 2; });";
    JSValue result;
    int32_t order;

    result = JS_Eval(retired, code, strlen(code), "retired", JS_EVAL_TYPE_GLOBAL);
    ck_assert(!JS_IsException(result));
    JS_FreeValue(retired, result);
    result = JS_Eval(active, code, strlen(code), "active", JS_EVAL_TYPE_GLOBAL);
    ck_assert(!JS_IsException(result));
    JS_FreeValue(active, result);

    JS_CloseContextJobs(retired);
    result = JS_Eval(retired, "order", 5, "retired", JS_EVAL_TYPE_GLOBAL);
    ck_assert_int_eq(JS_ToInt32(retired, &order, result), 0);
    ck_assert_int_eq(order, 0);
    JS_FreeValue(retired, result);
    JS_FreeContext(retired);
    JS_RunGC(rt);

    for (int i = 0; i < 2; i++) {
        ck_assert_int_eq(JS_ExecutePendingJob(rt, &executed), 1);
        ck_assert_ptr_eq(executed, active);
    }
    ck_assert(!JS_IsJobPending(rt));
    result = JS_Eval(active, "order", 5, "active", JS_EVAL_TYPE_GLOBAL);
    ck_assert_int_eq(JS_ToInt32(active, &order, result), 0);
    ck_assert_int_eq(order, 12);
    JS_FreeValue(active, result);
    JS_FreeContext(active);
    JS_FreeRuntime(rt);
}
END_TEST

START_TEST(test_quickjs_cancel_finalization_jobs)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    const char *code = "var finalized = 0;"
                       "var registry = new FinalizationRegistry(function() { finalized++; });"
                       "var target = {}; registry.register(target, 1);"
                       "Promise.resolve(target).then(function() {}); target = null;"
                       "var later = {}; later.self = later;"
                       "registry.register(later, 2); later = null;";
    JSValue result = JS_Eval(ctx, code, strlen(code), "finalization", JS_EVAL_TYPE_GLOBAL);
    int32_t finalized;

    ck_assert(!JS_IsException(result));
    JS_FreeValue(ctx, result);
    ck_assert(JS_IsJobPending(rt));

    /* The Promise job holds target alive. Releasing it during cancellation
     * attempts to queue a FinalizationRegistry callback for this context. */
    JS_CloseContextJobs(ctx);
    ck_assert_msg(!JS_IsJobPending(rt), "FinalizationRegistry queued a callback during cancellation");

    /* The unreachable cycle survives until GC: closing must also prevent
     * jobs from appearing after the cancellation pass has finished. */
    JS_RunGC(rt);
    ck_assert_msg(!JS_IsJobPending(rt), "FinalizationRegistry queued a callback after cancellation");
    result = JS_Eval(ctx, "finalized", 9, "finalization", JS_EVAL_TYPE_GLOBAL);
    ck_assert_int_eq(JS_ToInt32(ctx, &finalized, result), 0);
    ck_assert_int_eq(finalized, 0);
    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
END_TEST

/**
 * Test creating and destroying a heap.
 */
START_TEST(test_quickjs_heap_create_destroy)
{
    jsheap *heap = NULL;
    nserror err;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);
    ck_assert_ptr_nonnull(heap);

    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test creating and destroying a thread.
 */
START_TEST(test_quickjs_thread_create_destroy)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);
    ck_assert_ptr_nonnull(thread);

    err = js_closethread(thread);
    ck_assert_int_eq(err, NSERROR_OK);

    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test executing simple JavaScript code.
 */
START_TEST(test_quickjs_exec_simple)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test simple expression */
    const char *code = "1 + 1";
    result = js_exec(thread, (const uint8_t *)code, strlen(code), "test");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test executing JavaScript with syntax error.
 */
START_TEST(test_quickjs_exec_syntax_error)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test syntax error - should return false */
    const char *code = "function( { broken syntax";
    result = js_exec(thread, (const uint8_t *)code, strlen(code), "test_error");
    ck_assert(result == false);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test executing JavaScript that creates objects.
 */
START_TEST(test_quickjs_exec_objects)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test creating objects and arrays */
    const char *code = "var obj = { name: 'test', value: 42 };\n"
                       "var arr = [1, 2, 3];\n"
                       "obj.name + arr.length;";
    result = js_exec(thread, (const uint8_t *)code, strlen(code), "test_objects");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test console.log via js.h API (integration test).
 * This verifies the console binding is automatically initialized.
 */
START_TEST(test_quickjs_exec_console_log)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test console.log - should work now that it's auto-initialized */
    const char *code = "console.log('Integration test: console works via js.h!');";
    result = js_exec(thread, (const uint8_t *)code, strlen(code), "test_console");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test that execution on closed thread fails gracefully.
 */
START_TEST(test_quickjs_exec_closed_thread)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Close the thread first */
    err = js_closethread(thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Try to execute - should fail gracefully */
    const char *code = "1 + 1";
    result = js_exec(thread, (const uint8_t *)code, strlen(code), "test");
    ck_assert(result == false);

    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test multiple threads in one heap.
 */
START_TEST(test_quickjs_multiple_threads)
{
    jsheap *heap = NULL;
    jsthread *thread1 = NULL;
    jsthread *thread2 = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Create two threads */
    err = js_newthread(heap, NULL, NULL, &thread1);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread2);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Execute in both */
    const char *code = "var x = 1;";
    result = js_exec(thread1, (const uint8_t *)code, strlen(code), "test1");
    ck_assert(result == true);

    result = js_exec(thread2, (const uint8_t *)code, strlen(code), "test2");
    ck_assert(result == true);

    /* Clean up */
    js_closethread(thread1);
    js_closethread(thread2);
    js_destroythread(thread1);
    js_destroythread(thread2);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST


/*
 * Console binding tests - test the QuickJS console directly
 */

/**
 * Test initializing the console binding.
 */
START_TEST(test_quickjs_console_init)
{
    JSRuntime *rt;
    JSContext *ctx;
    int ret;

    rt = JS_NewRuntime();
    ck_assert_ptr_nonnull(rt);

    ctx = JS_NewContext(rt);
    ck_assert_ptr_nonnull(ctx);

    /* Initialize console binding */
    ret = qjs_init_console(ctx);
    ck_assert_int_eq(ret, 0);

    /* Verify console object exists */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_GetPropertyStr(ctx, global, "console");
    ck_assert(JS_IsObject(console));

    JS_FreeValue(ctx, console);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
END_TEST

/**
 * Test console.log() execution.
 */
START_TEST(test_quickjs_console_log)
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue result;

    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    qjs_init_console(ctx);

    /* Execute console.log - should not throw */
    const char *code = "console.log('Hello from QuickJS!');";
    result = JS_Eval(ctx, code, strlen(code), "test", JS_EVAL_TYPE_GLOBAL);

    ck_assert(!JS_IsException(result));

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
END_TEST

/**
 * Test console.error() execution.
 */
START_TEST(test_quickjs_console_error)
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue result;

    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    qjs_init_console(ctx);

    /* Execute console.error - should not throw */
    const char *code = "console.error('Test error message');";
    result = JS_Eval(ctx, code, strlen(code), "test", JS_EVAL_TYPE_GLOBAL);

    ck_assert(!JS_IsException(result));

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
END_TEST

/**
 * Test console.warn() execution.
 */
START_TEST(test_quickjs_console_warn)
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue result;

    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    qjs_init_console(ctx);

    /* Execute console.warn - should not throw */
    const char *code = "console.warn('Test warning');";
    result = JS_Eval(ctx, code, strlen(code), "test", JS_EVAL_TYPE_GLOBAL);

    ck_assert(!JS_IsException(result));

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
END_TEST

/**
 * Test console with multiple arguments.
 */
START_TEST(test_quickjs_console_multiple_args)
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue result;

    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    qjs_init_console(ctx);

    /* Execute console.log with multiple arguments */
    const char *code = "console.log('Value:', 42, 'Name:', 'test');";
    result = JS_Eval(ctx, code, strlen(code), "test", JS_EVAL_TYPE_GLOBAL);

    ck_assert(!JS_IsException(result));

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
END_TEST

/**
 * Test console.group() and console.groupEnd().
 */
START_TEST(test_quickjs_console_group)
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue result;

    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    qjs_init_console(ctx);

    /* Execute grouping */
    const char *code = "console.group();\n"
                       "console.log('Grouped message');\n"
                       "console.groupEnd();";
    result = JS_Eval(ctx, code, strlen(code), "test", JS_EVAL_TYPE_GLOBAL);

    ck_assert(!JS_IsException(result));

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
END_TEST

/**
 * Test Window global object basics.
 */
START_TEST(test_quickjs_window_global)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test 1: window global exists */
    const char *code1 = "typeof window !== 'undefined'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_window1");
    ck_assert(result == true);

    /* Test 2: window.self === window (self-reference) */
    const char *code2 = "window.self === window";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_window2");
    ck_assert(result == true);

    /* Test 3: window.window === window (self-reference) */
    const char *code3 = "window.window === window";
    result = js_exec(thread, (const uint8_t *)code3, strlen(code3), "test_window3");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Window methods exist (stubs).
 */
START_TEST(test_quickjs_window_methods)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test that alert is a function (from Window interface) */
    const char *code1 = "typeof window.alert === 'function'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_alert");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Timers (stubs).
 */
START_TEST(test_quickjs_timers)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test setTimeout exists and returns a number */
    const char *code1 = "typeof setTimeout === 'function'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_setTimeout");
    ck_assert(result == true);

    /* Test clearTimeout exists */
    const char *code2 = "typeof clearTimeout === 'function'";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_clearTimeout");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Navigator.
 */
START_TEST(test_quickjs_navigator)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test UserAgent */
    const char *code1 = "typeof navigator === 'object' && navigator.userAgent.length > 0";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_userAgent");
    ck_assert(result == true);

    /* Test cookieEnabled */
    const char *code2 = "navigator.cookieEnabled === true";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_cookieEnabled");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Location.
 */
START_TEST(test_quickjs_location)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test location exists */
    const char *code1 = "typeof location === 'object' && typeof window.location === 'object'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_location1");
    ck_assert(result == true);

    /* Test href */
    const char *code2 = "typeof location.href === 'string'";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_location2");
    ck_assert(result == true);

    /* Test replace/reload/assign methods */
    const char *code3 = "typeof location.replace === 'function' && "
                        "typeof location.reload === 'function' && "
                        "typeof location.assign === 'function'";
    result = js_exec(thread, (const uint8_t *)code3, strlen(code3), "test_location3");
    ck_assert(result == true);

    /* Test URL properties - should all be strings */
    const char *code4 = "typeof location.protocol === 'string' && "
                        "typeof location.host === 'string' && "
                        "typeof location.hostname === 'string' && "
                        "typeof location.port === 'string' && "
                        "typeof location.pathname === 'string' && "
                        "typeof location.search === 'string' && "
                        "typeof location.hash === 'string' && "
                        "typeof location.origin === 'string'";
    result = js_exec(thread, (const uint8_t *)code4, strlen(code4), "test_location_url_props");
    ck_assert(result == true);


    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Document.
 */
START_TEST(test_quickjs_document)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test document exists */
    const char *code1 = "typeof document === 'object' && typeof window.document === 'object'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_document1");
    ck_assert(result == true);

    /* Test getElementById stub */
    const char *code2 = "document.getElementById('foo') === null";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_getElementById");
    ck_assert(result == true);

    /* Test createElement stub */
    const char *code3 = "typeof document.createElement('div') === 'object'";
    result = js_exec(thread, (const uint8_t *)code3, strlen(code3), "test_createElement");
    ck_assert(result == true);

    /* Test write stub */
    const char *code4 = "typeof document.write === 'function'";
    result = js_exec(thread, (const uint8_t *)code4, strlen(code4), "test_write");
    ck_assert(result == true);

    /* Test body and documentElement properties */
    const char *code5 = "typeof document.body === 'object' && typeof document.documentElement === 'object'";
    result = js_exec(thread, (const uint8_t *)code5, strlen(code5), "test_doc_props");
    ck_assert(result == true);

    /* Test cookie */
    const char *code6 = "document.cookie === ''";
    result = js_exec(thread, (const uint8_t *)code6, strlen(code6), "test_cookie");
    ck_assert(result == true);

    /* Test element.style property exists */
    const char *code7 = "var el = document.createElement('div'); "
                        "typeof el.style === 'object'";
    result = js_exec(thread, (const uint8_t *)code7, strlen(code7), "test_element_style");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Storage (localStorage, sessionStorage).
 */
START_TEST(test_quickjs_storage)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test localStorage exists */
    const char *code1 = "typeof localStorage === 'object' && typeof localStorage.getItem === 'function'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_localStorage");
    ck_assert(result == true);

    /* Test sessionStorage exists */
    const char *code2 = "typeof sessionStorage === 'object' && typeof sessionStorage.setItem === 'function'";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_sessionStorage");
    ck_assert(result == true);

    /* Test storage.length */
    const char *code3 = "localStorage.length === 0";
    result = js_exec(thread, (const uint8_t *)code3, strlen(code3), "test_storage_length");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test EventTarget (addEventListener, removeEventListener).
 */
START_TEST(test_quickjs_event_target)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test addEventListener exists on window */
    const char *code1 = "typeof window.addEventListener === 'function'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_addEventListener");
    ck_assert(result == true);

    /* Test removeEventListener exists */
    const char *code2 = "typeof removeEventListener === 'function'";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_removeEventListener");
    ck_assert(result == true);

    /* Test dispatchEvent exists */
    const char *code3 = "typeof dispatchEvent === 'function'";
    result = js_exec(thread, (const uint8_t *)code3, strlen(code3), "test_dispatchEvent");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test XMLHttpRequest.
 */
START_TEST(test_quickjs_xhr)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;
    bool result;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test XMLHttpRequest constructor exists */
    const char *code1 = "typeof XMLHttpRequest === 'function'";
    result = js_exec(thread, (const uint8_t *)code1, strlen(code1), "test_xhr_ctor");
    ck_assert(result == true);

    /* Test creating XHR instance - just check it's an object */
    const char *code2 = "typeof (new XMLHttpRequest()) === 'object'";
    result = js_exec(thread, (const uint8_t *)code2, strlen(code2), "test_xhr_instance");
    ck_assert(result == true);

    /* Test open method */
    const char *code3 = "var xhr = new XMLHttpRequest(); xhr.open('GET', '/test'); xhr.readyState === 1";
    result = js_exec(thread, (const uint8_t *)code3, strlen(code3), "test_xhr_open");
    ck_assert(result == true);

    /* Test DONE constant */
    const char *code4 = "XMLHttpRequest.DONE === 4";
    result = js_exec(thread, (const uint8_t *)code4, strlen(code4), "test_xhr_const");
    ck_assert(result == true);

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

Suite *quickjs_suite(void)
{

    Suite *s;
    TCase *tc_core;
    TCase *tc_exec;
    TCase *tc_console;
    TCase *tc_window;

    s = suite_create("QuickJS");

    /* Core test case */
    tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_quickjs_init_finalise);
    tcase_add_test(tc_core, test_quickjs_heap_create_destroy);
    tcase_add_test(tc_core, test_quickjs_thread_create_destroy);
    tcase_add_test(tc_core, test_quickjs_multiple_threads);
    tcase_add_test(tc_core, test_quickjs_cancel_jobs);
    tcase_add_test(tc_core, test_quickjs_cancel_promise_jobs);
    tcase_add_test(tc_core, test_quickjs_cancel_finalization_jobs);
    suite_add_tcase(s, tc_core);

    /* Execution test case */
    tc_exec = tcase_create("Execution");
    tcase_add_test(tc_exec, test_quickjs_exec_simple);
    tcase_add_test(tc_exec, test_quickjs_exec_syntax_error);
    tcase_add_test(tc_exec, test_quickjs_exec_objects);
    tcase_add_test(tc_exec, test_quickjs_exec_console_log);
    tcase_add_test(tc_exec, test_quickjs_exec_closed_thread);
    suite_add_tcase(s, tc_exec);

    /* Console binding test case */
    tc_console = tcase_create("Console");
    tcase_add_test(tc_console, test_quickjs_console_init);
    tcase_add_test(tc_console, test_quickjs_console_log);
    tcase_add_test(tc_console, test_quickjs_console_error);
    tcase_add_test(tc_console, test_quickjs_console_warn);
    tcase_add_test(tc_console, test_quickjs_console_multiple_args);
    tcase_add_test(tc_console, test_quickjs_console_group);
    suite_add_tcase(s, tc_console);

    /* Window binding test case */
    tc_window = tcase_create("Window");
    tcase_add_test(tc_window, test_quickjs_window_global);
    tcase_add_test(tc_window, test_quickjs_window_methods);
    tcase_add_test(tc_window, test_quickjs_timers);
    tcase_add_test(tc_window, test_quickjs_navigator);
    tcase_add_test(tc_window, test_quickjs_location);
    tcase_add_test(tc_window, test_quickjs_document);
    tcase_add_test(tc_window, test_quickjs_storage);
    tcase_add_test(tc_window, test_quickjs_event_target);
    tcase_add_test(tc_window, test_quickjs_xhr);
    suite_add_tcase(s, tc_window);

    return s;
}


int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = quickjs_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_VERBOSE);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
