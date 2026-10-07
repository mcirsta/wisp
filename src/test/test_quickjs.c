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
#include <stdio.h>
#include <stddef.h>
#include <nsutils/time.h>
#include <string.h>
#include <time.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include "content/handlers/javascript/quickjs/private.h"
#include <wisp/content/handlers/html/private.h>

#include "content/handlers/javascript/js.h"

/* Include QuickJS directly for console binding tests */
#include "content/handlers/javascript/quickjs/console.h"
#include "content/handlers/javascript/quickjs/window.h"
#include "content/handlers/javascript/quickjs/timers.h"
#include "content/handlers/javascript/quickjs/navigator.h"
#include "content/handlers/javascript/quickjs/location.h"
#include "content/handlers/javascript/quickjs/document.h"
#include "content/handlers/javascript/quickjs/storage.h"
#include "content/handlers/javascript/quickjs/event_target.h"
#include "content/handlers/javascript/quickjs/xhr.h"
#include "quickjs.h"

/* js_exec reports completion, not the truth of the evaluated expression. */
static bool eval_predicate(jsthread *thread, const char *expression, const char *name)
{
    const char *format = "if (!(%s)) throw new Error('test predicate was false');";
    size_t size = strlen(expression) + strlen(format) + 1;
    char *script = malloc(size);
    ck_assert_ptr_nonnull(script);
    snprintf(script, size, format, expression);
    bool result = js_exec(thread, (const uint8_t *)script, strlen(script), name);
    free(script);
    return result;
}

static void assert_predicate(jsthread *thread, const char *expression, const char *name)
{
    ck_assert_msg(eval_predicate(thread, expression, name), "%s: %s", name, expression);
}

static bool exec_script(jsthread *thread, const char *script, const char *name)
{
    return js_exec(thread, (const uint8_t *)script, strlen(script), name);
}

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

START_TEST(test_quickjs_cancel_microtasks)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *retired = JS_NewContext(rt);
    JSContext *active = JS_NewContext(rt);
    JSContext *executed = NULL;
    const char *code = "var order = 0;"
                       "queueMicrotask(function() { order = order * 10 + 1; });"
                       "Promise.resolve().then(function() { order = order * 10 + 2; });"
                       "queueMicrotask(function() { order = order * 10 + 3; });";
    JSValue result;
    int32_t order;

    result = JS_Eval(retired, code, strlen(code), "retired", JS_EVAL_TYPE_GLOBAL);
    ck_assert_msg(!JS_IsException(result), "Retired context failed to enqueue mixed microtasks");
    JS_FreeValue(retired, result);
    result = JS_Eval(active, code, strlen(code), "active", JS_EVAL_TYPE_GLOBAL);
    ck_assert_msg(!JS_IsException(result), "Active context failed to enqueue mixed microtasks");
    JS_FreeValue(active, result);

    JS_CloseContextJobs(retired);
    /* Closing also rejects queueMicrotask calls made after cancellation. */
    result = JS_Eval(retired, code, strlen(code), "retired", JS_EVAL_TYPE_GLOBAL);
    ck_assert(!JS_IsException(result));
    JS_FreeValue(retired, result);
    JS_FreeContext(retired);
    JS_RunGC(rt);

    for (int i = 0; i < 3; i++) {
        ck_assert_ptr_eq(JS_GetPendingJobContext(rt), active);
        ck_assert_int_eq(JS_ExecutePendingJob(rt, &executed), 1);
        ck_assert_ptr_eq(executed, active);
    }
    ck_assert(!JS_IsJobPending(rt));
    result = JS_Eval(active, "order", 5, "active", JS_EVAL_TYPE_GLOBAL);
    ck_assert_int_eq(JS_ToInt32(active, &order, result), 0);
    ck_assert_int_eq(order, 123);
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

START_TEST(test_quickjs_discard_jobs)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt), *other = JS_NewContext(rt), *executed;
    const char *code = "var registry = new FinalizationRegistry(() => { throw 'discarded finalizer'; });"
                       "var target = {}; registry.register(target, 1);"
                       "Promise.resolve(target).then(() => { throw 'discarded job'; }); target = null;";
    JSValue result = JS_Eval(ctx, code, strlen(code), "discard", JS_EVAL_TYPE_GLOBAL);
    ck_assert(!JS_IsException(result));
    JS_FreeValue(ctx, result);
    JSValue one = JS_NewInt32(other, 1), two = JS_NewInt32(ctx, 2);
    executed_job_order = 0;
    ck_assert_int_eq(JS_EnqueueJob(other, record_job, 1, &one), 0);
    JS_DiscardContextJobs(ctx);
    ck_assert_int_eq(executed_job_order, 0);
    /* Discard suppresses finalization while releasing arguments, but permits
     * a later independent entry to enqueue jobs on this same context. */
    ck_assert_int_eq(JS_EnqueueJob(ctx, record_job, 1, &two), 0);
    ck_assert_int_eq(JS_ExecutePendingJob(rt, &executed), 1);
    ck_assert_ptr_eq(executed, other);
    ck_assert_int_eq(JS_ExecutePendingJob(rt, &executed), 1);
    ck_assert_ptr_eq(executed, ctx);
    ck_assert_int_eq(executed_job_order, 12);
    ck_assert(!JS_IsJobPending(rt));
    JS_CloseContextJobs(ctx);
    JS_DiscardContextJobs(ctx); /* Must not reopen a retired document. */
    ck_assert_int_eq(JS_EnqueueJob(ctx, record_job, 1, &two), 0);
    ck_assert(!JS_IsJobPending(rt));
    JS_FreeContext(ctx);
    JS_FreeContext(other);
    JS_FreeRuntime(rt);
}
END_TEST

START_TEST(test_quickjs_microtask_checkpoint)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(exec_script(thread,
        "var order = ['script'];"
        "Promise.resolve().then(() => { order.push('promise'); queueMicrotask(() => order.push('nested')); });"
        "queueMicrotask(() => order.push('microtask'));"
        "(async function() { await 0; order.push('await'); })();"
        "order.push('end');",
        "checkpoint"));
    assert_predicate(thread, "order.join(',') === 'script,end,promise,microtask,await,nested'", "FIFO checkpoint");
    ck_assert(!exec_script(thread,
        "queueMicrotask(() => order.push('after throw')); throw new Error('expected synchronous error');",
        "throwing script"));
    assert_predicate(thread, "order.at(-1) === 'after throw'", "exception still checkpoints");
    ck_assert(exec_script(thread,
        "queueMicrotask(() => { throw new Error('expected job error'); });"
        "queueMicrotask(() => order.push('after job error'));"
        "var rejected = Promise.reject(new Error('handled in same checkpoint'));"
        "queueMicrotask(() => rejected.catch(() => order.push('handled')));",
        "job exceptions"));
    assert_predicate(
        thread, "order.slice(-2).join(',') === 'after job error,handled'", "jobs continue after exception");
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

START_TEST(test_quickjs_microtask_timeout)
{
    static const char *loops[] = {
        "var spins = 0; function spin() { spins++; queueMicrotask(spin); } queueMicrotask(spin);",
        "var spins = 0; function spin() { spins++; Promise.resolve().then(spin); } spin();",
        "var spins = 0; Promise.resolve().then(() => { spins++; while (true) {} });",
    };
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    for (unsigned i = 0; i < sizeof(loops) / sizeof(loops[0]); i++) {
        uint64_t start, end;
        ck_assert_int_eq(nsu_getmonotonic_ms(&start), NSUERROR_OK);
        ck_assert_msg(!exec_script(thread, loops[i], "runaway checkpoint"), "Loop %u completed", i);
        ck_assert_int_eq(nsu_getmonotonic_ms(&end), NSUERROR_OK);
        ck_assert_msg(end - start >= 900 && end - start < 5000, "Loop %u: one-second checkpoint took %llu ms", i,
            (unsigned long long)(end - start));
        ck_assert(exec_script(thread,
            "var stoppedAt = spins; var recovered = false; Promise.resolve().then(() => { recovered = true; });",
            "new checkpoint after timeout"));
        assert_predicate(thread, "recovered && spins === stoppedAt && spins > 0", "discarded loop and fresh jobs");
    }
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

START_TEST(test_quickjs_microtask_shared_budget)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    /* Each part fits the budget alone; together they exceed it. This tests
     * budget sharing without a tight assertion on the host's elapsed time. */
    ck_assert(!exec_script(thread,
        "var jobStarted = false, jobFinished = false;"
        "var scriptStart = Date.now(); while (Date.now() - scriptStart < 600) {}"
        "queueMicrotask(() => { jobStarted = true; var start = Date.now();"
        "while (Date.now() - start < 600) {} jobFinished = true; });",
        "shared deadline"));
    assert_predicate(thread, "jobStarted && !jobFinished", "checkpoint did not renew deadline");
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

/** Test creating and destroying a heap. */
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
    const char *code = "1 + 1 === 2";
    result = eval_predicate(thread, code, "test arithmetic");
    ck_assert(result);

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
                       "if (obj.name !== 'test' || arr.length !== 3) throw new Error('objects');";
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

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test 1: window global exists */
    const char *code1 = "typeof window !== 'undefined'";
    assert_predicate(thread, code1, "test_window1");

    /* Test 2: window.self === window (self-reference) */
    const char *code2 = "window.self === window";
    assert_predicate(thread, code2, "test_window2");

    /* Test 3: window.window === window (self-reference) */
    const char *code3 = "window.window === window";
    assert_predicate(thread, code3, "test_window3");

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Window methods exist (stubs).
 */
START_TEST(test_quickjs_unsupported_dialogs)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Dialogs are currently unsupported; the old completion-only test hid this. */
    const char *code1 = "typeof window.alert === 'undefined' && typeof window.confirm === 'undefined' && "
                        "typeof window.prompt === 'undefined'";
    assert_predicate(thread, code1, "test_alert");

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/* A deterministic scheduler harness: real monotonic deadlines, manually run
 * callbacks. Browser integration tests separately use the actual Qt scheduler. */
static struct {
    void (*callback)(void *);
    void *pw;
    uint64_t due;
} timer_schedules[16];
static struct wisp_table *saved_guit;
static bool fail_schedule, preserve_failed_schedule;
static unsigned timer_callbacks;

static nserror test_timer_schedule(int delay, void (*callback)(void *), void *pw)
{
    if (delay >= 0 && fail_schedule && preserve_failed_schedule)
        return NSERROR_NOMEM;
    unsigned slot = 16;
    for (unsigned i = 0; i < 16; i++) {
        if (timer_schedules[i].callback == callback && timer_schedules[i].pw == pw)
            timer_schedules[i].callback = NULL;
        if (timer_schedules[i].callback == NULL)
            slot = i;
    }
    if (delay < 0)
        return NSERROR_OK;
    if (fail_schedule)
        return NSERROR_NOMEM;
    ck_assert_msg(slot < 16, "Too many document wakeups");
    uint64_t now;
    ck_assert_int_eq(nsu_getmonotonic_ms(&now), NSUERROR_OK);
    timer_schedules[slot].callback = callback;
    timer_schedules[slot].pw = pw;
    timer_schedules[slot].due = now + (unsigned)delay;
    return NSERROR_OK;
}

static void setup_timers(void)
{
    static struct gui_misc_table misc = {.schedule = test_timer_schedule};
    static struct wisp_table table = {.misc = &misc};
    memset(timer_schedules, 0, sizeof(timer_schedules));
    saved_guit = guit;
    guit = &table;
    fail_schedule = false;
    preserve_failed_schedule = false;
    timer_callbacks = 0;
}

static void teardown_timers(void)
{
    for (unsigned i = 0; i < 16; i++)
        ck_assert_msg(timer_schedules[i].callback == NULL, "Document left a scheduled callback behind");
    guit = saved_guit;
}

static bool run_next_timer(void)
{
    unsigned slot = 16;
    for (unsigned i = 0; i < 16; i++) {
        if (timer_schedules[i].callback != NULL && (slot == 16 || timer_schedules[i].due < timer_schedules[slot].due))
            slot = i;
    }
    if (slot == 16)
        return false;
    uint64_t now;
    do {
        ck_assert_int_eq(nsu_getmonotonic_ms(&now), NSUERROR_OK);
        ck_assert_msg(timer_schedules[slot].due <= now + 100, "Unexpected long timer in test");
        if (now < timer_schedules[slot].due) {
            struct timespec pause = {.tv_nsec = 1000000};
            nanosleep(&pause, NULL);
        }
    } while (now < timer_schedules[slot].due);
    void (*callback)(void *) = timer_schedules[slot].callback;
    void *pw = timer_schedules[slot].pw;
    timer_schedules[slot].callback = NULL;
    timer_callbacks++;
    callback(pw);
    return true;
}

static void drain_timers(unsigned limit)
{
    while (limit-- > 0 && run_next_timer()) {
    }
    for (unsigned i = 0; i < 16; i++)
        ck_assert_msg(timer_schedules[i].callback == NULL, "Timer queue did not settle within test limit");
}

START_TEST(test_quickjs_timers)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(exec_script(thread,
        "var order = [], argument = {};"
        "var a = setTimeout(function(x, y) { 'use strict';"
        "if (this !== globalThis || x !== argument || y !== 42) throw Error('arguments/receiver');"
        "order.push('a'); queueMicrotask(() => order.push('job')); }, 0, argument, 42);"
        "var b = setTimeout(() => order.push('b'), 0);"
        "var cancelled = setTimeout(() => order.push('cancelled'), 0); clearInterval(cancelled);"
        "clearTimeout(); clearInterval(-1); clearTimeout(1234567);"
        "order.push('script');",
        "timers.js"));
    assert_predicate(thread, "order.join(',') === 'script' && a > 0 && b > 0 && a !== b", "no synchronous callback");
    drain_timers(4);
    assert_predicate(thread, "order.join(',') === 'script,a,job,b'", "FIFO, arguments and checkpoint");
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

START_TEST(test_quickjs_timer_conversion)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(exec_script(thread,
        "var conversion = [], ran = []; var sentinel = {};"
        "setTimeout({toString() { conversion.push('handler'); return \"ran.push('string');\"; }},"
        "{valueOf() { conversion.push('delay'); return -5; }});"
        "try { setTimeout({toString() { throw sentinel; }}, {valueOf() { conversion.push('wrong'); }});"
        "throw Error('missing throw'); } catch(e) { if (e !== sentinel) throw e; }"
        "try { setTimeout(() => {}, {valueOf() { throw sentinel; }}); throw Error('missing delay throw'); }"
        "catch(e) { if (e !== sentinel) throw e; }"
        "for (var invalid of [() => setTimeout(), () => setInterval(), () => setTimeout(Symbol()),"
        "() => setTimeout(() => {}, 1n), () => setTimeout.call({}, () => {}), () => clearTimeout.call({}, 1)]) {"
        "var failed = false; try { invalid(); } catch(e) { failed = e instanceof TypeError; }"
        "if (!failed) throw Error('bad Web IDL conversion'); }"
        "setTimeout(() => ran.push('wrapped'), 2147483648);"
        "setTimeout(() => ran.push('nan'), NaN); setTimeout(() => ran.push('infinity'), Infinity);",
        "conversion.js"));
    assert_predicate(thread, "conversion.join(',') === 'handler,delay'", "conversion order");
    drain_timers(6);
    assert_predicate(thread, "ran.join(',') === 'string,wrapped,nan,infinity'", "global strings and long conversion");
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

START_TEST(test_quickjs_timer_intervals)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(exec_script(thread,
        "var calls = 0, jobs = 0; var interval = setInterval(() => {"
        "calls++; Promise.resolve().then(() => { jobs++; if (calls === 8) clearTimeout(interval); });"
        "if (calls === 2) throw Error('expected interval exception'); }, 0);",
        "interval.js"));
    drain_timers(10);
    assert_predicate(thread, "calls === 8 && jobs === 8", "zero-delay interval, throw and self-cancellation");
    ck_assert_uint_eq(timer_callbacks, 8);
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

static unsigned observed_nesting[10];
static uint64_t observed_timer_times[10];
static unsigned nesting_count;
static JSValue record_timer_nesting(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    ck_assert_uint_lt(nesting_count, 10);
    ck_assert_int_eq(nsu_getmonotonic_ms(&observed_timer_times[nesting_count]), NSUERROR_OK);
    observed_nesting[nesting_count++] = thread->heap->timer_nesting;
    return JS_UNDEFINED;
}

START_TEST(test_quickjs_timer_nesting)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    JSValue global = JS_GetGlobalObject(thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(thread->ctx, global, "recordNesting",
                         JS_NewCFunction(thread->ctx, record_timer_nesting, "recordNesting", 0)),
        1);
    JS_FreeValue(thread->ctx, global);
    nesting_count = 0;
    ck_assert(exec_script(thread,
        "var n = 0; function nested() { recordNesting(); if (++n < 7) setTimeout(nested, 0);"
        "else queueMicrotask(() => { recordNesting(); setTimeout(recordNesting, 0); }); }"
        "setTimeout(nested, 0);",
        "nested.js"));
    drain_timers(10);
    const unsigned expected[] = {1, 2, 3, 4, 5, 6, 6, 0, 1};
    ck_assert_uint_eq(nesting_count, sizeof(expected) / sizeof(expected[0]));
    ck_assert_msg(observed_timer_times[6] - observed_timer_times[5] >= 4, "Seventh nested timer ran before 4 ms");
    for (unsigned i = 0; i < nesting_count; i++)
        ck_assert_msg(observed_nesting[i] == expected[i], "Callback %u nesting=%u expected=%u", i, observed_nesting[i],
            expected[i]);
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

static JSValue retire_timer_document(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    js_destroyheap(thread->heap);
    js_destroythread(thread);
    return JS_UNDEFINED;
}

START_TEST(test_quickjs_timer_retirement)
{
    jsheap *heap;
    jsthread *thread, *other;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &other), NSERROR_OK);
    JSValue global = JS_GetGlobalObject(thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(thread->ctx, global, "retire",
                         JS_NewCFunction(thread->ctx, retire_timer_document, "retire", 0)),
        1);
    JS_FreeValue(thread->ctx, global);
    ck_assert(exec_script(thread,
        "setTimeout(() => { retire(); queueMicrotask(() => { throw Error('retired job ran'); }); }, 0);"
        "setInterval(() => { throw Error('retired timer ran'); }, 0);",
        "retire.js"));
    ck_assert(exec_script(other, "var alive = false; setTimeout(() => alive = true, 0);", "other.js"));
    drain_timers(4); /* The first callback releases the owner, including the heap owner reference. */
    ck_assert_uint_eq(timer_callbacks, 2);
    assert_predicate(other, "alive", "other document survives retirement");
    js_destroythread(other); /* Last heap reference. */
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_timer_failures)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    fail_schedule = true;
    ck_assert(!exec_script(thread, "setTimeout(() => {}, 0);", "scheduler OOM"));
    fail_schedule = false;
    ck_assert(exec_script(thread, "var ran = false; setTimeout(() => ran = true, 0);", "scheduler recovery"));
    drain_timers(2);
    assert_predicate(thread, "ran", "queue recovers after scheduler failure");
    /* Backends may either preserve or remove an old wakeup on failed rearm.
     * Neither may leave callbacks referring to the cancelled document queue. */
    for (int preserve = 0; preserve < 2; preserve++) {
        ck_assert(exec_script(thread, "setTimeout(() => { throw Error('stranded timer ran'); }, 10000);", "old wakeup"));
        preserve_failed_schedule = preserve;
        fail_schedule = true;
        ck_assert(!exec_script(thread, "setTimeout(() => {}, 0);", "failed rearm"));
        fail_schedule = false;
        ck_assert(!run_next_timer());
    }
    /* Force ID wrap and ensure a live ID is not reused. */
    ck_assert(exec_script(thread, "var first = setTimeout(() => {}, 50);", "first ID"));
    thread->next_timer_id = INT32_MAX;
    ck_assert(
        exec_script(thread, "var last = setTimeout(() => {}, 50), wrapped = setTimeout(() => {}, 50);", "wrapped IDs"));
    assert_predicate(thread, "first !== last && first !== wrapped && last !== wrapped && wrapped > 0", "unique IDs");
    js_closethread(thread);
    ck_assert(!run_next_timer());
    js_destroythread(thread);
    js_destroyheap(heap);
}
END_TEST

START_TEST(test_quickjs_timer_execution_limits)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(exec_script(thread,
        "var calls = 0, recovered = false; setInterval(() => { calls++; while (true) {} }, 0);"
        "setTimeout(() => { recovered = true; }, 0);",
        "runaway-timer.js"));
    drain_timers(4);
    assert_predicate(thread, "calls === 1 && recovered", "interrupted interval stops, sibling timer runs");
    ck_assert(exec_script(thread,
        "var afterJob = false; setInterval(() => {"
        "function spin() { queueMicrotask(spin); } queueMicrotask(spin); }, 0);"
        "setTimeout(() => afterJob = true, 0);",
        "runaway-timer-checkpoint.js"));
    drain_timers(4);
    assert_predicate(thread, "afterJob", "timer checkpoint shares interrupt protection");
    js_destroythread(thread);
    js_destroyheap(heap);
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

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test UserAgent */
    const char *code1 = "typeof navigator === 'object' && navigator.userAgent.length > 0";
    assert_predicate(thread, code1, "test_userAgent");

    /* Test cookieEnabled */
    const char *code2 = "navigator.cookieEnabled === true";
    assert_predicate(thread, code2, "test_cookieEnabled");

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

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test location exists */
    const char *code1 = "typeof location === 'object' && typeof window.location === 'object'";
    assert_predicate(thread, code1, "test_location1");

    /* Test href */
    const char *code2 = "typeof location.href === 'string'";
    assert_predicate(thread, code2, "test_location2");

    /* Test replace/reload/assign methods */
    const char *code3 = "typeof location.replace === 'function' && "
                        "typeof location.reload === 'function' && "
                        "typeof location.assign === 'function'";
    assert_predicate(thread, code3, "test_location3");

    /* Test URL properties - should all be strings */
    const char *code4 = "typeof location.protocol === 'string' && "
                        "typeof location.host === 'string' && "
                        "typeof location.hostname === 'string' && "
                        "typeof location.port === 'string' && "
                        "typeof location.pathname === 'string' && "
                        "typeof location.search === 'string' && "
                        "typeof location.hash === 'string' && "
                        "typeof location.origin === 'string'";
    assert_predicate(thread, code4, "test_location_url_props");


    assert_predicate(thread,
        "(() => { let sentinel = {}; for (let method of ['assign', 'replace']) {"
        "let caught = false; try { location[method]({toString() { throw sentinel; }}); }"
        "catch(e) { caught = e === sentinel; } if (!caught) return false; } return true; })()",
        "navigation URL conversion preserves exceptions");

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

/**
 * Test Document.
 */
static html_content dom_html;
static jsheap *dom_heap;
static jsthread *dom_thread;

static void setup_dom(void)
{
    memset(&dom_html, 0, sizeof(dom_html));
    const char *source = "<!doctype html><html><head><title>Example</title></head><body>"
        "<div id='target' class='old'>A<span>B</span><!--ignored--><i></i>C</div>"
        "<p id='duplicate'>first</p><p id='duplicate'>second</p><p id=''></p>"
        "<svg xmlns='http://www.w3.org/2000/svg' id='svg'><text id='svgText'>svg</text></svg></body></html>";
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &dom_html.document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)source, strlen(source)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    ck_assert_int_eq(js_newheap(1, &dom_heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(dom_heap, NULL, &dom_html, &dom_thread), NSERROR_OK);
}

static void teardown_dom(void)
{
    js_destroythread(dom_thread);
    JS_RunGC(dom_heap->rt); /* Exercise cycles while the shared runtime survives. */
    js_destroyheap(dom_heap);
    dom_node_unref(dom_html.document);
    js_finalise();
}

START_TEST(test_quickjs_document)
{
    assert_predicate(dom_thread,
        "document === window.document && document instanceof HTMLDocument && "
        "document instanceof Document && document instanceof Node && "
        "document.nodeType === Node.DOCUMENT_NODE && document.nodeName === '#document' && document.ownerDocument === null && "
        "Object.getPrototypeOf(HTMLElement) === Element && Object.getPrototypeOf(Element) === Node",
        "document identity and prototypes");
    assert_predicate(dom_thread,
        "document.documentElement === document.documentElement && document.documentElement.tagName === 'HTML' && "
        "document.head.tagName === 'HEAD' && document.body.tagName === 'BODY' && "
        "document.head.nextSibling === document.body && document.body.previousSibling === document.head && "
        "document.documentElement.parentNode === document && document.documentElement.parentElement === null && "
        "document.body.ownerDocument === document && document.body instanceof HTMLElement && "
        "document.body instanceof Element && document.body instanceof Node",
        "root traversal and inheritance");
    assert_predicate(dom_thread,
        "document.getElementById('target') === document.body.firstChild && document.getElementById('absent') === null && "
        "document.getElementById('') === null && document.getElementById('duplicate').textContent === 'first' && "
        "document.getElementById('svgText').tagName === 'text' && "
        "!(document.getElementById('svgText') instanceof HTMLElement)", "actual ID lookup and foreign elements");
    ck_assert(exec_script(dom_thread, "document.body.expando = { owner: document.body };", "expando"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "document.body.expando.owner === document.body", "GC preserves node identity/properties");
    assert_predicate(dom_thread,
        "document.querySelector === undefined && document.createElementNS === undefined && "
        "document.body.style === undefined",
        "unsupported APIs do not fabricate success");
}
END_TEST

START_TEST(test_quickjs_dom_attributes)
{
    ck_assert(exec_script(dom_thread,
        "var element = document.getElementById('target');"
        "element.setAttribute('DATA-X', 'ș😀\\0');"
        "element.setAttribute('1:valid<name', 'allowed');"
        "if (element.getAttribute('1:valid<name') !== 'allowed') throw Error('attribute local name');"
        "if (element.getAttribute('data-x') !== 'ș😀\\0' || !element.hasAttribute('DATA-X')) throw Error('attribute');"
        "element.id = 'renamed'; element.className = 'a b';"
        "if (document.getElementById('target') !== null || document.getElementById('renamed') !== element || "
        "element.getAttribute('class') !== 'a b') throw Error('reflection');"
        "element.removeAttribute('DATA-X'); element.removeAttribute('absent');"
        "if (element.getAttribute('data-x') !== null || element.hasAttribute('DATA-X')) throw Error('remove');"
        "element.removeAttribute('class'); if (element.className !== '') throw Error('missing class');",
        "attributes.js"));
    ck_assert(exec_script(dom_thread,
        "var invalid; try { element.setAttribute('has space', 'x'); } catch (e) { invalid = e; }"
        "if (!(invalid instanceof DOMException) || !(invalid instanceof Error) || "
        "invalid.name !== 'InvalidCharacterError' || invalid.code !== 5) throw Error('DOM exception');"
        "try { Element.prototype.getAttribute.call(document, 'id'); throw Error('no brand check'); }"
        "catch (e) { if (!(e instanceof TypeError)) throw e; }",
        "attribute-errors.js"));
}
END_TEST

START_TEST(test_quickjs_dom_text)
{
    ck_assert(exec_script(dom_thread,
        "var target = document.getElementById('target'), oldText = target.firstChild;"
        "if (target.textContent !== 'ABC' || !(oldText instanceof Text) || "
        "!(oldText instanceof CharacterData) || oldText.data !== 'A') throw Error('initial text');"
        "target.textContent = 'ș😀';"
        "if (target.firstChild !== target.lastChild || target.firstChild.nodeValue !== 'ș😀' || "
        "oldText.parentNode !== null || oldText.isConnected || !target.isConnected) throw Error('replace all');"
        "oldText.data = 'detached'; if (oldText.textContent !== 'detached') throw Error('detached text');"
        "oldText.data = null; if (oldText.data !== '') throw Error('legacy null');"
        "oldText.data = undefined; if (oldText.data !== 'undefined') throw Error('undefined data');"
        "oldText.data = 'detached';"
        "target.textContent = null;"
        "if (target.firstChild !== null || target.hasChildNodes() || target.textContent !== '') throw Error('empty');"
        "target.nodeValue = 'ignored'; document.textContent = 'ignored';"
        "if (target.nodeValue !== null || target.textContent !== '' || document.textContent !== null) throw Error('no-op');",
        "text.js"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "oldText.data === 'detached' && oldText.ownerDocument === document", "detached node survives GC");
}
END_TEST

START_TEST(test_quickjs_dom_conversion)
{
    ck_assert(exec_script(dom_thread,
        "var target = document.getElementById('target'), sentinel = {}, converted = false;"
        "try { target.setAttribute({toString() { throw sentinel; }}, {toString() { converted = true; }}); }"
        "catch (e) { if (e !== sentinel) throw Error('lost exception'); }"
        "if (converted) throw Error('conversion order');"
        "try { target.textContent = {toString() { throw sentinel; }}; } catch (e) { if (e !== sentinel) throw e; }"
        "if (target.textContent !== 'ABC') throw Error('failed conversion changed DOM');"
        "for (var operation of [() => target.getAttribute(), () => document.getElementById(), "
        "() => target.setAttribute('id'), () => target.setAttribute(Symbol(), 'x')]) {"
        " var caught; try { operation(); } catch (e) { caught = e; }"
        " if (!(caught instanceof TypeError)) throw Error('required arguments/conversions'); }",
        "conversion.js"));
}
END_TEST

START_TEST(test_quickjs_dom_retirement)
{
    ck_assert(exec_script(dom_thread, "var old = document.body; old.cycle = {node: old};", "retain.js"));
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue old = JS_GetPropertyStr(dom_thread->ctx, global, "old");
    js_closethread(dom_thread);
    /* Directly exercise a surviving wrapper: the public entry correctly refuses
     * a retired document, so it cannot be used for this native lifetime check. */
    JSValue value = JS_GetPropertyStr(dom_thread->ctx, old, "nodeType");
    ck_assert(JS_IsException(value));
    JS_FreeValue(dom_thread->ctx, JS_GetException(dom_thread->ctx));
    JS_FreeValue(dom_thread->ctx, old);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_dom_runtime_isolation)
{
    jsheap *second_heap;
    jsthread *second;
    ck_assert_int_eq(js_newheap(1, &second_heap), NSERROR_OK);
    JSClassID foreign = 0;
    JS_NewClassID(second_heap->rt, &foreign);
    JSClassDef definition = {.class_name = "UnrelatedHostClass"};
    ck_assert_int_eq(JS_NewClass(second_heap->rt, foreign, &definition), 0);
    ck_assert_int_eq(js_newthread(second_heap, NULL, &dom_html, &second), NSERROR_OK);
    ck_assert(dom_heap->node_class != second_heap->node_class);
    for (unsigned i = 0; i < 2; i++) {
        jsthread *thread = i == 0 ? dom_thread : second;
        assert_predicate(thread, "document.body instanceof HTMLElement && new XMLHttpRequest().readyState === 0",
            "runtime-local host classes");
        ck_assert(exec_script(thread, "console.group('group'); console.log('class identity'); console.groupEnd();",
            "generated console class"));
        assert_predicate(thread, "(() => { try { new XMLHttpRequest().open.call(document, 'GET', '/'); } "
            "catch (e) { return e instanceof TypeError; } return false; })()", "XHR cannot read a DOM wrapper payload");
    }
    js_destroythread(second);
    js_destroyheap(second_heap);
}
END_TEST

static JSValue close_dom_during_conversion(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    js_closethread(JS_GetContextOpaque(ctx));
    return JS_UNDEFINED;
}

START_TEST(test_quickjs_dom_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
        JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)), 1);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var element = document.getElementById('target');"
        "var caught; try { element.textContent = {toString() { retire(); return 'after retirement'; }}; }"
        "catch (e) { caught = e; }"
        "if (!(caught instanceof DOMException) || caught.name !== 'InvalidStateError') throw Error('retired mutation');",
        "retiring-conversion.js"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

START_TEST(test_quickjs_classlist_live)
{
    ck_assert(exec_script(dom_thread,
        "var el = document.getElementById('target'), list = el.classList;"
        "if (list !== el.classList || !(list instanceof DOMTokenList) || Array.isArray(list)) throw Error('identity');"
        "if (Object.prototype.toString.call(list) !== '[object DOMTokenList]') throw Error('brand');"
        "el.className = '  alpha\\tbeta\\nș😀\\ralpha\\fgamma\\u00a0delta  ';"
        "if (list.length !== 4 || list.item(2) !== 'ș😀' || list[3] !== 'gamma\\u00a0delta') throw Error('tokens');"
        "if (String(list) !== el.className || list.value !== el.className) throw Error('raw value');"
        "list.add('ș😀', 'nul\\0token');"
        "if (el.className !== 'alpha beta ș😀 gamma\\u00a0delta nul\\0token') throw Error('byte lengths');"
        "el.setAttribute('class', 'x x y');"
        "if (list.length !== 2 || list[0] !== 'x' || list[1] !== 'y') throw Error('attribute update');"
        "el.removeAttribute('class');"
        "if (list.length !== 0 || list.value !== '' || list.item(0) !== null || list[0] !== undefined) throw Error('removal');"
        "var svg = document.getElementById('svg'); svg.classList.add('one', 'two');"
        "if (svg.getAttribute('class') !== 'one two' || svg.classList.length !== 2) throw Error('SVG classes');",
        "classList identity, live attributes and Unicode"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "list === el.classList", "classList identity survives GC");
}
END_TEST

START_TEST(test_quickjs_classlist_mutation)
{
    ck_assert(exec_script(dom_thread,
        "var el = document.getElementById('target'), l = el.classList;"
        "el.removeAttribute('class'); l.add(); l.remove(); l.remove('absent');"
        "if (el.hasAttribute('class')) throw Error('absent attribute created');"
        "l.value = ' a  a b '; l.add(); if (l.value !== 'a b') throw Error('zero argument normalization');"
        "l.value = ' a  a b '; l.add('a'); if (l.value !== 'a b') throw Error('existing add normalization');"
        "l.value = ' a  a b '; l.remove('missing'); if (l.value !== 'a b') throw Error('absent remove normalization');"
        "l.value = ' a  a b '; if (!l.toggle('a', true) || l.value !== ' a  a b ') throw Error('forced no-op');"
        "if (l.toggle('missing', false) || l.value !== ' a  a b ') throw Error('forced absent no-op');"
        "if (l.toggle('a') || l.value !== 'b' || !l.toggle('a') || l.value !== 'b a') throw Error('toggle');"
        "if (l.toggle('a', undefined) || l.value !== 'b') throw Error('undefined force is missing');"
        "if (!l.toggle('a', {}) || l.toggle('a', null)) throw Error('boolean force conversion');"
        "l.value = 'a b c d'; if (!l.replace('b', 'd') || l.value !== 'a d c') throw Error('replace later duplicate');"
        "l.value = 'a b c d'; if (!l.replace('d', 'b') || l.value !== 'a b c') throw Error('replace earlier duplicate');"
        "l.value = ' a  a '; if (!l.replace('a', 'a') || l.value !== 'a') throw Error('same replacement');"
        "l.value = ' a  a '; if (l.replace('absent', 'b') || l.value !== ' a  a ') throw Error('absent replacement');"
        "l.value = 'a b c'; if (l.remove('b', 'c', 'b') !== undefined || l.value !== 'a') throw Error('remove');"
        "el.classList = ' x  y '; if (l.value !== ' x  y ' || el.classList !== l) throw Error('PutForwards');"
        "l.value = null; if (l.value !== 'null') throw Error('null DOMString');"
        "l.value = undefined; if (l.value !== 'undefined') throw Error('undefined DOMString');"
        "var forwarded; Object.defineProperty(l, 'value', {set(v) { forwarded = v; }});"
        "var marker = {}; el.classList = marker; if (forwarded !== marker) throw Error('forward without coercion');"
        "const nativeSetter = Object.getOwnPropertyDescriptor(Element.prototype, 'classList').set;"
        "const readonly = {}; Object.defineProperty(readonly, 'value', {value:42});"
        "Object.defineProperty(el, 'classList', {value:readonly, configurable:true});"
        "nativeSetter.call(el, 'ignored'); if (readonly.value !== 42) throw Error('forward rejected write');"
        "Object.defineProperty(el, 'classList', {value:17}); let rejected = false;"
        "try { nativeSetter.call(el, 'x'); } catch(e) { rejected = e instanceof TypeError; }"
        "if (!rejected) throw Error('forward primitive');",
        "classList mutation algorithms"));
}
END_TEST

START_TEST(test_quickjs_classlist_errors)
{
    ck_assert(exec_script(dom_thread,
        "var el = document.getElementById('target'), l = el.classList;"
        "function raises(name, fn) { let e; try { fn(); } catch(x) { e = x; }"
        "if (!e || e.name !== name) throw Error('expected ' + name); }"
        "for (const op of ['add', 'remove']) {"
        "raises('SyntaxError', () => l[op]('good', ''));"
        "raises('InvalidCharacterError', () => l[op]('good', 'bad\\tvalue'));"
        "if (el.className !== 'old') throw Error('partial mutation'); }"
        "raises('SyntaxError', () => l.toggle(''));"
        "raises('SyntaxError', () => l.replace('bad value', ''));"
        "raises('InvalidCharacterError', () => l.replace('old', 'bad\\nvalue'));"
        "for (const op of ['contains', 'toggle', 'replace', 'item', 'supports']) raises('TypeError', () => l[op]());"
        "raises('TypeError', () => l.replace('old'));"
        "raises('TypeError', () => l.supports('old'));"
        "raises('TypeError', () => new DOMTokenList());"
        "raises('TypeError', () => l.add.call(el, 'x'));"
        "raises('TypeError', () => Object.getOwnPropertyDescriptor(DOMTokenList.prototype, 'length').get.call({}));"
        "if (l.contains('') || l.contains('bad value')) throw Error('contains validates unnecessarily');"
        "if (l.item(-1) !== null || l.item(4294967296) !== 'old' || l.item(NaN) !== 'old') throw Error('index conversion');"
        "raises('TypeError', () => l.item(Symbol())); raises('TypeError', () => l.item(0n));",
        "classList validation, required arguments and receiver checks"));
}
END_TEST

START_TEST(test_quickjs_classlist_coercion)
{
    ck_assert(exec_script(dom_thread,
        "var el = document.getElementById('target'), l = el.classList, sentinel = {}, order = [];"
        "var caught; try { l.add('', {toString() { throw sentinel; }}); } catch(e) { caught = e; }"
        "if (caught !== sentinel || el.className !== 'old') throw Error('conversion before validation');"
        "try { l.add('valid', 'bad value', {toString() { order.push('last'); return 'last'; }}); } catch(e) {}"
        "if (order.join() !== 'last' || el.className !== 'old') throw Error('convert all before mutation');"
        "l.add({toString() { el.className = 'from-coercion'; return 'added'; }});"
        "if (el.className !== 'from-coercion added') throw Error('stale parsed cache');"
        "try { l.remove('added', Symbol()); } catch(e) { caught = e; }"
        "if (!(caught instanceof TypeError) || el.className !== 'from-coercion added') throw Error('conversion exception');"
        "try { l.supports({toString() { throw sentinel; }}); } catch(e) { caught = e; }"
        "if (caught !== sentinel) throw Error('supports conversion order');",
        "classList coercion and exception order"));
}
END_TEST

START_TEST(test_quickjs_classlist_indexed)
{
    ck_assert(exec_script(dom_thread,
        "var l = document.getElementById('target').classList; l.value = 'a b';"
        "l.note = 42; const sym = Symbol('extra'); l[sym] = 'symbol';"
        "const keys = Reflect.ownKeys(l); if (keys.length !== 4 || keys[0] !== '0' || keys[1] !== '1' ||"
        "keys[2] !== 'note' || keys[3] !== sym) throw Error('key order');"
        "const d = Object.getOwnPropertyDescriptor(l, '0');"
        "if (!d.enumerable || !d.configurable || d.writable || d.value !== 'a' || !('0' in l) || '2' in l) throw Error('descriptor');"
        "l[0] = 'wrong'; l[99] = 'wrong'; if (l[0] !== 'a' || l[99] !== undefined) throw Error('indexed write');"
        "if (Reflect.defineProperty(l, '0', {value:'wrong'}) || Reflect.defineProperty(l, '99', {value:'wrong'})) throw Error('define index');"
        "for (const key of ['0','99']) { let threw = false;"
        "try { (function() {'use strict'; l[key] = 'wrong'; })(); } catch(e) { threw = e instanceof TypeError; }"
        "if (!threw) throw Error('strict index write ' + key);"
        "threw = false; try { Object.defineProperty(l, key, {value:'wrong'}); } catch(e) { threw = e instanceof TypeError; }"
        "if (!threw) throw Error('throwing define ' + key); }"
        "if (Reflect.deleteProperty(l, '0') || !Reflect.deleteProperty(l, '99')) throw Error('delete index');"
        "l['01'] = 'leading'; l['4294967295'] = 'not-index';"
        "if (l['01'] !== 'leading' || l['4294967295'] !== 'not-index') throw Error('ordinary keys');"
        "Object.defineProperty(l, 'ordinary', {value:1, configurable:false});"
        "if (Object.getOwnPropertyDescriptor(l, 'ordinary').configurable || Reflect.deleteProperty(l, 'ordinary')) throw Error('expando descriptor');"
        "if (Reflect.preventExtensions(l) || !Object.isExtensible(l)) throw Error('must remain extensible');"
        "let threw = false; try { Object.freeze(l); } catch(e) { threw = e instanceof TypeError; }"
        "if (!threw || !Object.isExtensible(l)) throw Error('freeze live object');"
        "l.value = 'c'; if (l[0] !== 'c' || l[1] !== undefined || '1' in l) throw Error('live indices');",
        "classList indexed platform properties"));
}
END_TEST

START_TEST(test_quickjs_classlist_iteration)
{
    ck_assert(exec_script(dom_thread,
        "var l = document.getElementById('target').classList; l.value = 'a b';"
        "if (l.values !== Array.prototype.values || l[Symbol.iterator] !== l.values ||"
        "l.forEach !== Array.prototype.forEach) throw Error('Web IDL iteration methods');"
        "if ([...l].join() !== 'a,b' || [...l.keys()].join() !== '0,1' || [...l.entries()][1].join() !== '1,b') throw Error('iteration');"
        "var iterator = l.values(); if (iterator.next().value !== 'a') throw Error('first');"
        "l.add('c'); if (iterator.next().value !== 'b' || iterator.next().value !== 'c' || !iterator.next().done) throw Error('live iterator');"
        "l.add('d'); if (!iterator.next().done) throw Error('finished iterator');"
        "l.value = 'a b'; const marker = {}, seen = [];"
        "l.forEach(function(value, index, object) { if (this !== marker || object !== l) throw Error('callback args');"
        "seen.push(index + value); if (index === 0) l.add('c'); }, marker);"
        "if (seen.join() !== '0a,1b') throw Error('forEach initial length');",
        "classList standard array iterators and callbacks"));
}
END_TEST

START_TEST(test_quickjs_classlist_retirement)
{
    ck_assert(exec_script(dom_thread,
        "var child = document.getElementById('target').firstChild.nextSibling, l = child.classList;"
        "l.cycle = {owner: child, list: l}; document.getElementById('target').textContent = '';"
        "l.add('detached'); if (child.isConnected || l.value !== 'detached') throw Error('detached list');",
        "detached classList"));
    JS_RunGC(dom_heap->rt);
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue list = JS_GetPropertyStr(dom_thread->ctx, global, "l");
    js_closethread(dom_thread);
    JSValue length = JS_GetPropertyStr(dom_thread->ctx, list, "length");
    ck_assert(JS_IsException(length));
    JS_FreeValue(dom_thread->ctx, JS_GetException(dom_thread->ctx));
    JS_FreeValue(dom_thread->ctx, list);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_classlist_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
        JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)), 1);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var l = document.getElementById('target').classList, caught;"
        "try { l.add({toString() { retire(); return 'x'; }}); } catch(e) { caught = e; }"
        "if (!caught || caught.name !== 'InvalidStateError') throw Error('retired classList mutation');",
        "classList retirement during coercion"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

START_TEST(test_quickjs_node_creation)
{
    ck_assert(exec_script(dom_thread,
        "var el = document.createElement('DiV', {}), text = document.createTextNode('ș😀\\0'), "
        "comment = document.createComment('<!-- anything -->'), fragment = document.createDocumentFragment();"
        "if (!(el instanceof HTMLElement) || el.tagName !== 'DIV' || el.ownerDocument !== document || "
        "el.parentNode !== null || el.isConnected || el.classList.length !== 0) throw Error('element creation');"
        "if (!(text instanceof Text) || !(text instanceof CharacterData) || text.nodeType !== 3 || "
        "text.nodeName !== '#text' || text.data !== 'ș😀\\0') throw Error('text creation');"
        "if (!(comment instanceof Comment) || comment.data !== '<!-- anything -->') throw Error('comment creation');"
        "if (!(fragment instanceof DocumentFragment) || !(fragment instanceof Node) || "
        "fragment.nodeType !== 11 || fragment.nodeName !== '#document-fragment' || fragment.ownerDocument !== document || "
        "fragment.isConnected || fragment.textContent !== '') throw Error('fragment creation');"
        "fragment.textContent = 'fragment text'; if (fragment.firstChild.data !== 'fragment text') throw Error('fragment text');"
        "fragment.textContent = ''; if (fragment.hasChildNodes()) throw Error('clear fragment');"
        "if (document.createTextNode(null).data !== 'null' || document.createComment(undefined).data !== 'undefined') "
        "throw Error('DOMString conversion');"
        "el.id = 'created'; document.body.appendChild(el);"
        "if (document.getElementById('created') !== el || !el.isConnected) throw Error('connected identity');",
        "node creation and prototypes"));
    /* The native HTML factory must use the HTML namespace too, so selection and
     * HTML element specialization do not depend on a binding-only workaround. */
    dom_node *body = NULL, *element = NULL;
    dom_string *namespace = NULL;
    ck_assert_int_eq(dom_html_document_get_body(dom_html.document, &body), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_last_child(body, &element), DOM_NO_ERR);
    ck_assert_int_eq(dom_node_get_namespace(element, &namespace), DOM_NO_ERR);
    ck_assert_ptr_nonnull(namespace);
    ck_assert_str_eq(dom_string_data(namespace), "http://www.w3.org/1999/xhtml");
    dom_string_unref(namespace);
    dom_node_unref(element);
    dom_node_unref(body);
}
END_TEST

START_TEST(test_quickjs_node_creation_errors)
{
    ck_assert(exec_script(dom_thread,
        "function expect(name, fn) { try { fn(); } catch(e) { if (e.name === name) return; throw e; } "
        "throw Error('missing ' + name); }"
        "for (const name of ['', '1name', 'bad name', 'bad\\tname', 'bad\\nname', 'bad\\rname', 'bad\\fname', "
        "'bad/name', 'bad>name', 'bad\\0name', '_bad<name']) "
        "expect('InvalidCharacterError', () => document.createElement(name));"
        "for (const name of ['span', 'x:part', 'a<b=c', '_valid-name', 'ș😀', 'a\\u00a0b']) "
        "if (!(document.createElement(name) instanceof HTMLElement)) throw Error('valid local name');"
        "for (const method of ['createElement', 'createTextNode', 'createComment']) {"
        "expect('TypeError', () => document[method]());"
        "expect('TypeError', () => document[method].call({}, 'x'));"
        "expect('TypeError', () => document[method](Symbol()));"
        "let sentinel = {}; try { document[method]({toString() { throw sentinel; }}); throw Error('converted'); }"
        "catch(e) { if (e !== sentinel) throw e; }}"
        "expect('NotSupportedError', () => document.createElement('button', {is: 'custom-button'}));"
        "let order = ''; document.createElement({toString() { order += 'n'; return 'div'; }}, {"
        "get customElementRegistry() { order += 'r'; return null; }, get is() { order += 'i'; return undefined; }});"
        "if (order !== 'nri') throw Error('dictionary conversion order');"
        "let sentinel = {}; try { document.createElement('div', {get is() { throw sentinel; }}); "
        "throw Error('option getter'); } catch(e) { if (e !== sentinel) throw e; }",
        "creation validation and coercion"));
}
END_TEST

START_TEST(test_quickjs_tree_moves)
{
    ck_assert(exec_script(dom_thread,
        "var parent = document.createElement('div'), a = document.createTextNode('a'), "
        "b = document.createTextNode('b'), c = document.createTextNode('c');"
        "if (parent.appendChild(a) !== a || parent.appendChild(c) !== c || parent.insertBefore(b, c) !== b) "
        "throw Error('insert returns');"
        "if (parent.textContent !== 'abc' || a.nextSibling !== b || c.previousSibling !== b) throw Error('links');"
        "parent.insertBefore(c, a); if (parent.textContent !== 'cab' || parent.firstChild !== c) throw Error('move before');"
        "parent.appendChild(c); if (parent.textContent !== 'abc' || parent.lastChild !== c) throw Error('move after');"
        "if (parent.replaceChild(c, a) !== a || a.parentNode !== null || parent.textContent !== 'cb' || "
        "c.nextSibling !== b || b.previousSibling !== c) throw Error('replace sibling');"
        "if (parent.replaceChild(c, c) !== c || parent.insertBefore(c, c) !== c) throw Error('self replacement');"
        "if (parent.removeChild(b) !== b || b.isConnected || b.parentNode !== null || b.previousSibling !== null || "
        "b.nextSibling !== null || parent.lastChild !== c) throw Error('removal');"
        "parent.insertBefore(a, null); parent.insertBefore(b, undefined);"
        "if (parent.textContent !== 'cab') throw Error('nullable reference');"
        "parent.id = 'made'; document.body.appendChild(parent); parent.expando = {child: a};"
        "var holder = document.createDocumentFragment(); holder.appendChild(parent);"
        "if (parent.isConnected || document.getElementById('made') !== null || parent.parentNode !== holder) "
        "throw Error('move into detached tree');",
        "tree edits preserve identities and links"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread,
        "parent.expando.child === a && a.parentNode === parent && holder.firstChild === parent",
        "detached subtrees survive GC");
}
END_TEST

START_TEST(test_quickjs_tree_fragments)
{
    ck_assert(exec_script(dom_thread,
        "var p = document.createElement('div'), f = document.createDocumentFragment(), "
        "a = document.createTextNode('a'), b = document.createTextNode('b'), c = document.createTextNode('c');"
        "p.appendChild(a); p.appendChild(b); p.appendChild(c);"
        "if (p.replaceChild(f, b) !== b || p.textContent !== 'ac' || a.nextSibling !== c || "
        "c.previousSibling !== a || b.parentNode !== null) throw Error('empty fragment replaces middle');"
        "p.replaceChild(f, a); p.replaceChild(f, c);"
        "if (p.firstChild !== null || p.lastChild !== null) throw Error('empty fragment replaces edges');"
        "f.appendChild(a); f.appendChild(b); f.appendChild(c);"
        "if (p.appendChild(f) !== f || f.hasChildNodes() || f.parentNode !== null || p.textContent !== 'abc' || "
        "a.parentNode !== p || c.parentNode !== p) throw Error('fragment transfer');"
        "for (let i = 0; i < 30; i++) { f.appendChild(a); f.appendChild(b); f.appendChild(c); p.appendChild(f); }"
        "var replacement = document.createDocumentFragment(); replacement.textContent = 'X';"
        "var x = replacement.firstChild; p.replaceChild(replacement, b);"
        "if (p.textContent !== 'aXc' || x.parentNode !== p || x.previousSibling !== a || x.nextSibling !== c || "
        "replacement.hasChildNodes()) throw Error('nonempty fragment replacement');",
        "fragment insertion replacement and repeated reuse"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "f.textContent === '' && p.textContent === 'aXc' && b.parentNode === null",
        "fragment ownership after transfers");
}
END_TEST

START_TEST(test_quickjs_tree_validation)
{
    ck_assert(exec_script(dom_thread,
        "function expect(name, fn) { try { fn(); } catch(e) { if (e.name === name) return; throw e; } "
        "throw Error('missing ' + name); }"
        "var parent = document.createElement('div'), child = document.createElement('span'), "
        "foreign = document.createElement('i'), fragment = document.createDocumentFragment(); parent.appendChild(child);"
        "expect('HierarchyRequestError', () => child.appendChild(parent));"
        "expect('HierarchyRequestError', () => parent.appendChild(parent));"
        "expect('HierarchyRequestError', () => child.appendChild(document));"
        "expect('HierarchyRequestError', () => document.createTextNode('').appendChild(fragment));"
        "expect('HierarchyRequestError', () => document.createComment('').appendChild(child));"
        "expect('NotFoundError', () => parent.insertBefore(child, foreign));"
        "expect('NotFoundError', () => parent.replaceChild(child, foreign));"
        "expect('NotFoundError', () => parent.removeChild(foreign));"
        "expect('TypeError', () => parent.appendChild({__proto__: Node.prototype}));"
        "expect('TypeError', () => parent.insertBefore(child));"
        "expect('TypeError', () => parent.replaceChild(child, null));"
        "expect('TypeError', () => parent.removeChild());"
        "expect('TypeError', () => parent.appendChild.call({}, child));"
        "expect('TypeError', () => parent.appendChild(null));"
        "if (parent.firstChild !== child || parent.lastChild !== child || child.parentNode !== parent || "
        "foreign.parentNode !== null || fragment.hasChildNodes()) throw Error('failure changed tree');",
        "tree validation is atomic and brand checked"));
}
END_TEST

START_TEST(test_quickjs_document_tree_validation)
{
    ck_assert(exec_script(dom_thread,
        "function hierarchy(fn) { try { fn(); } catch(e) { if (e.name === 'HierarchyRequestError') return; throw e; } "
        "throw Error('missing hierarchy error'); }"
        "var root = document.documentElement, dt = document.firstChild, f = document.createDocumentFragment(), "
        "a = document.createElement('html'), b = document.createElement('html'), text = document.createTextNode('');"
        "hierarchy(() => document.appendChild(a)); hierarchy(() => document.appendChild(text));"
        "hierarchy(() => document.body.appendChild(dt));"
        "hierarchy(() => document.insertBefore(a, dt));"
        "if (document.removeChild(root) !== root || document.documentElement !== null || document.body !== null) "
        "throw Error('remove root');"
        "f.appendChild(a); f.appendChild(b); hierarchy(() => document.appendChild(f));"
        "if (f.firstChild !== a || f.lastChild !== b || a.parentNode !== f || document.documentElement !== null) "
        "throw Error('invalid fragment changed trees');"
        "f.removeChild(b); f.appendChild(text); hierarchy(() => document.appendChild(f)); f.removeChild(text);"
        "document.appendChild(f); if (document.documentElement !== a || f.hasChildNodes()) throw Error('fragment root');"
        "if (document.replaceChild(root, a) !== a || document.documentElement !== root || a.parentNode !== null) "
        "throw Error('replace root');"
        "if (document.replaceChild(root, root) !== root) throw Error('replace root itself');"
        "hierarchy(() => document.appendChild(root));"
        "document.removeChild(dt); hierarchy(() => document.appendChild(dt)); document.insertBefore(dt, root);"
        "hierarchy(() => document.replaceChild(a, dt));"
        "document.replaceChild(f, root); if (document.documentElement !== null) throw Error('clear root');"
        "hierarchy(() => document.insertBefore(root, dt)); document.appendChild(root);",
        "document root doctype order and fragment constraints"));
}
END_TEST

START_TEST(test_quickjs_creation_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
                         JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)),
        1);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var p = document.createElement('div'), f = document.createDocumentFragment(); f.appendChild(p);"
        "var caught; try { document.createElement('div', {get is() { retire(); return undefined; }}); } "
        "catch(e) { caught = e; } if (!caught || caught.name !== 'InvalidStateError') throw Error('retired creation');",
        "retirement during element options conversion"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
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

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test localStorage exists */
    const char *code1 = "typeof localStorage === 'object' && typeof localStorage.getItem === 'function'";
    assert_predicate(thread, code1, "test_localStorage");

    /* Test sessionStorage exists */
    const char *code2 = "typeof sessionStorage === 'object' && typeof sessionStorage.setItem === 'function'";
    assert_predicate(thread, code2, "test_sessionStorage");

    /* Test storage.length */
    const char *code3 = "localStorage.length === 0";
    assert_predicate(thread, code3, "test_storage_length");

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

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test addEventListener exists on window */
    const char *code1 = "typeof window.addEventListener === 'function'";
    assert_predicate(thread, code1, "test_addEventListener");

    /* Test removeEventListener exists */
    const char *code2 = "typeof removeEventListener === 'function'";
    assert_predicate(thread, code2, "test_removeEventListener");

    /* Test dispatchEvent exists */
    const char *code3 = "typeof dispatchEvent === 'function'";
    assert_predicate(thread, code3, "test_dispatchEvent");

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

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test XMLHttpRequest constructor exists */
    const char *code1 = "typeof XMLHttpRequest === 'function'";
    assert_predicate(thread, code1, "test_xhr_ctor");

    /* Test creating XHR instance - just check it's an object */
    const char *code2 = "typeof (new XMLHttpRequest()) === 'object'";
    assert_predicate(thread, code2, "test_xhr_instance");

    /* Test open method */
    const char *code3 = "(() => { var xhr = new XMLHttpRequest(); xhr.open('GET', '/test'); "
                        "return xhr.readyState === 1; })()";
    assert_predicate(thread, code3, "test_xhr_open");

    /* Test DONE constant */
    const char *code4 = "XMLHttpRequest.DONE === 4";
    assert_predicate(thread, code4, "test_xhr_const");

    js_closethread(thread);
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_xhr_conversion_exception)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    const char *code = "var xhr = new XMLHttpRequest();"
                       "var sentinel = new Error('conversion failed');"
                       "var invalid = { toString: function() { throw sentinel; } };"
                       "xhr.open('GET', '/before');"
                       "for (var args of [[invalid, '/test'], ['GET', invalid]]) {"
                       "  var caught = null;"
                       "  try { xhr.open(args[0], args[1]); } catch (e) { caught = e; }"
                       "  if (caught !== sentinel) throw new Error('conversion exception lost');"
                       "  if (xhr.readyState !== 1) throw new Error('request changed after exception');"
                       "}"
                       "xhr.open('POST', '/after');";

    js_initialise();
    ck_assert_int_eq(js_newheap(5, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(js_exec(thread, (const uint8_t *)code, strlen(code), "xhr conversion"));
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_predicate_helper)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(exec_script(thread, "false", "normal false completion"));
    ck_assert(eval_predicate(thread, "true", "true predicate"));
    ck_assert(!eval_predicate(thread, "false", "false predicate"));
    ck_assert(!eval_predicate(thread, "undefined", "undefined predicate"));
    ck_assert(!eval_predicate(thread, "(() => { throw new Error('expected'); })()", "throwing predicate"));
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_timeout_recovery)
{
    jsheap *heap;
    jsthread *thread, *other;
    uint64_t start, end;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &other), NSERROR_OK);

    /* Real deadlines, not instruction-count or mocked-clock substitutes.
     * A second run verifies that interruption does not poison later entries. */
    for (int attempt = 0; attempt < 2; attempt++) {
        ck_assert_int_eq(nsu_getmonotonic_ms(&start), NSUERROR_OK);
        ck_assert_msg(!exec_script(thread,
                          "var caught = false, finished = false, jobRan = false;"
                          "Promise.resolve().then(() => { jobRan = true; });"
                          "try { while (true) {} } catch (e) { caught = true; }"
                          "finally { finished = true; }",
                          "runaway.js"),
            "Runaway script completed on attempt %d", attempt);
        ck_assert_int_eq(nsu_getmonotonic_ms(&end), NSUERROR_OK);
        ck_assert_msg(end - start >= 900 && end - start < 5000, "Attempt %d: one-second budget took %llu ms", attempt,
            (unsigned long long)(end - start));
        assert_predicate(thread, "!caught && !finished && !jobRan", "uncatchable interruption without job drain");
        assert_predicate(other, "1 + 1 === 2", "sibling document remains usable");
    }
    /* A nested native-to-JS coercion must remain inside the same budget. */
    ck_assert(
        !exec_script(thread, "var continued = false; console.log({toString() { while (true) {} }}); continued = true;",
            "native coercion loop"));
    assert_predicate(thread, "!continued && 2 + 2 === 4", "recovery after native coercion");
    js_closethread(thread);
    ck_assert(!exec_script(thread, "throw new Error('closed document ran');", "closed document"));
    js_destroythread(thread);
    assert_predicate(other, "typeof XMLHttpRequest === 'function'", "sibling after retirement");
    js_destroythread(other);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_console_conversion_exception)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(exec_script(thread,
        "var sentinel = new Error('conversion');"
        "for (var method of ['log', 'error', 'warn', 'info', 'debug', 'dir']) {"
        "  var caught; var later = false;"
        "  try { console[method]('prefix', {toString() {throw sentinel;}},"
        "                         {toString() {later = true; return 'later';}}); }"
        "  catch (e) {caught = e;}"
        "  if (caught !== sentinel || later) throw new Error('lost conversion failure in ' + method);"
        "}",
        "console conversion"));
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_binding_allocation_failure)
{
    static const struct {
        const char *name;
        int (*init)(JSContext *ctx);
    } bindings[] = {
        {"console", qjs_init_console},
        {"window", qjs_init_window},
        {"timers", qjs_init_timers},
        {"navigator", qjs_init_navigator},
        {"location", qjs_init_location},
        {"document", qjs_init_document},
        {"storage", qjs_init_storage},
        {"events", qjs_init_event_target},
        {"xhr", qjs_init_xhr},
    };

    /* Sweep small allocation budgets through registration, object creation and
     * property installation. Exercise the real generator output too. This is
     * bounded OOM coverage, not a claim to inject failure at every allocation. */
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        bool succeeded = false;
        for (size_t extra = 0; extra <= 65536; extra += 256) {
            JSRuntime *rt = JS_NewRuntime();
            ck_assert_ptr_nonnull(rt);
            JSContext *ctx = JS_NewContext(rt);
            ck_assert_ptr_nonnull(ctx);
            JSMemoryUsage usage;
            JS_ComputeMemoryUsage(rt, &usage);
            JS_SetMemoryLimit(rt, usage.malloc_size + extra);
            int ret = bindings[i].init(ctx);
            bool exception = JS_HasException(ctx);
            ck_assert_msg(
                ret < 0 || !exception, "%s reported success with an exception (budget %zu)", bindings[i].name, extra);
            ck_assert_msg(extra != 0 || ret < 0, "%s ignored the exhausted allocation budget", bindings[i].name);
            JS_FreeValue(ctx, JS_GetException(ctx));
            JS_SetMemoryLimit(rt, 0); /* QuickJS uses zero to disable its allocation limit. */
            JS_FreeContext(ctx);

            /* A failed document must not poison registration for its successor. */
            ctx = JS_NewContext(rt);
            ck_assert_ptr_nonnull(ctx);
            ck_assert_msg(bindings[i].init(ctx) == 0 && !JS_HasException(ctx), "%s did not recover after budget %zu",
                bindings[i].name, extra);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt); /* Debug QuickJS asserts that no objects leaked. */
            if (ret == 0) {
                succeeded = true;
                break;
            }
        }
        ck_assert_msg(succeeded, "%s never initialized within the test's allocation sweep", bindings[i].name);
    }
}
END_TEST

START_TEST(test_quickjs_exception_diagnostics)
{
    jsheap *heap;
    jsthread *thread;
    const char *scripts[] = {
        "var inspected = false; throw {toString() { inspected = true; while (true) {} }};",
        "var e = new Error('original message');"
        "Object.defineProperty(e, 'stack', {get() { inspected = true; while (true) {} }});"
        "e.toString = function() { inspected = true; throw 42; }; throw e;",
        "throw new Proxy(new Error('proxy'), {getOwnPropertyDescriptor() { inspected = true; while (true) {} },"
        "get() { inspected = true; while (true) {} }});",
        "throw 'plain string';",
        "throw null;",
    };
    ck_assert_int_eq(js_newheap(1, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    for (size_t i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++) {
        ck_assert_msg(!exec_script(thread, scripts[i], "exception diagnostics"), "Exception %zu was lost", i);
        assert_predicate(thread, "!inspected", "diagnostics did not call page code");
    }
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_memory_stack_limits)
{
    jsheap *heap;
    jsthread *thread;
    ck_assert_int_eq(js_newheap(5, &heap), NSERROR_OK);
    ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
    ck_assert(!exec_script(thread, "new ArrayBuffer(128 * 1024 * 1024);", "runtime allocation limit"));
    assert_predicate(thread, "new ArrayBuffer(32).byteLength === 32", "allocation recovery");
    ck_assert(!exec_script(thread, "function recurse() { return recurse(); } recurse();", "stack limit"));
    assert_predicate(thread, "1 + 2 === 3", "stack recovery");
    js_destroythread(thread);
    js_destroyheap(heap);
    js_finalise();
}
END_TEST

START_TEST(test_quickjs_disabled_timeout)
{
    for (int timeout = -1; timeout <= 0; timeout++) {
        jsheap *heap;
        jsthread *thread;
        ck_assert_int_eq(js_newheap(timeout, &heap), NSERROR_OK);
        ck_assert_int_eq(js_newthread(heap, NULL, NULL, &thread), NSERROR_OK);
        assert_predicate(thread, "(() => { for (var i = 0; i < 100000; i++) {} return true; })()", "no deadline");
        js_destroythread(thread);
        js_destroyheap(heap);
    }
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
    TCase *tc_surface;
    TCase *tc_limits;

    s = suite_create("QuickJS");

    /* Core test case */
    tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_quickjs_init_finalise);
    tcase_add_test(tc_core, test_quickjs_heap_create_destroy);
    tcase_add_test(tc_core, test_quickjs_thread_create_destroy);
    tcase_add_test(tc_core, test_quickjs_multiple_threads);
    tcase_add_test(tc_core, test_quickjs_cancel_jobs);
    tcase_add_test(tc_core, test_quickjs_discard_jobs);
    tcase_add_test(tc_core, test_quickjs_cancel_promise_jobs);
    tcase_add_test(tc_core, test_quickjs_cancel_microtasks);
    tcase_add_test(tc_core, test_quickjs_cancel_finalization_jobs);
    suite_add_tcase(s, tc_core);

    /* Execution test case */
    tc_exec = tcase_create("Execution");
    tcase_add_test(tc_exec, test_quickjs_exec_simple);
    tcase_add_test(tc_exec, test_quickjs_microtask_checkpoint);
    tcase_add_test(tc_exec, test_quickjs_exec_syntax_error);
    tcase_add_test(tc_exec, test_quickjs_exec_objects);
    tcase_add_test(tc_exec, test_quickjs_exec_console_log);
    tcase_add_test(tc_exec, test_quickjs_exec_closed_thread);
    suite_add_tcase(s, tc_exec);

    /* Console binding test case */
    tc_console = tcase_create("Console");
    tcase_add_test(tc_console, test_quickjs_console_conversion_exception);
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
    suite_add_tcase(s, tc_window);

    TCase *tc_timers = tcase_create("Timers");
    tcase_set_timeout(tc_timers, 10);
    tcase_add_checked_fixture(tc_timers, setup_timers, teardown_timers);
    tcase_add_test(tc_timers, test_quickjs_timers);
    tcase_add_test(tc_timers, test_quickjs_timer_conversion);
    tcase_add_test(tc_timers, test_quickjs_timer_intervals);
    tcase_add_test(tc_timers, test_quickjs_timer_nesting);
    tcase_add_test(tc_timers, test_quickjs_timer_retirement);
    tcase_add_test(tc_timers, test_quickjs_timer_failures);
    tcase_add_test(tc_timers, test_quickjs_timer_execution_limits);
    suite_add_tcase(s, tc_timers);

    /* These assert the current stub surface, not Web API conformance. Replace
     * them with behavioral tests as real implementations replace the stubs. */
    tc_surface = tcase_create("Stub API surface (not conformance)");
    tcase_add_test(tc_surface, test_quickjs_unsupported_dialogs);

    tcase_add_test(tc_surface, test_quickjs_navigator);
    tcase_add_test(tc_surface, test_quickjs_location);
    TCase *tc_dom = tcase_create("Real DOM");
    tcase_add_checked_fixture(tc_dom, setup_dom, teardown_dom);
    tcase_add_test(tc_dom, test_quickjs_document);
    tcase_add_test(tc_dom, test_quickjs_dom_attributes);
    tcase_add_test(tc_dom, test_quickjs_dom_text);
    tcase_add_test(tc_dom, test_quickjs_dom_conversion);
    tcase_add_test(tc_dom, test_quickjs_dom_retirement);
    tcase_add_test(tc_dom, test_quickjs_dom_runtime_isolation);
    tcase_add_test(tc_dom, test_quickjs_dom_conversion_retirement);
    tcase_add_test(tc_dom, test_quickjs_classlist_live);
    tcase_add_test(tc_dom, test_quickjs_classlist_mutation);
    tcase_add_test(tc_dom, test_quickjs_classlist_errors);
    tcase_add_test(tc_dom, test_quickjs_classlist_coercion);
    tcase_add_test(tc_dom, test_quickjs_classlist_indexed);
    tcase_add_test(tc_dom, test_quickjs_classlist_iteration);
    tcase_add_test(tc_dom, test_quickjs_classlist_retirement);
    tcase_add_test(tc_dom, test_quickjs_classlist_conversion_retirement);
    tcase_add_test(tc_dom, test_quickjs_node_creation);
    tcase_add_test(tc_dom, test_quickjs_node_creation_errors);
    tcase_add_test(tc_dom, test_quickjs_tree_moves);
    tcase_add_test(tc_dom, test_quickjs_tree_fragments);
    tcase_add_test(tc_dom, test_quickjs_tree_validation);
    tcase_add_test(tc_dom, test_quickjs_document_tree_validation);
    tcase_add_test(tc_dom, test_quickjs_creation_retirement);
    suite_add_tcase(s, tc_dom);
    tcase_add_test(tc_surface, test_quickjs_storage);
    tcase_add_test(tc_surface, test_quickjs_event_target);
    tcase_add_test(tc_surface, test_quickjs_xhr);
    tcase_add_test(tc_surface, test_quickjs_xhr_conversion_exception);
    suite_add_tcase(s, tc_surface);

    tc_limits = tcase_create("Execution limits and recovery");
    tcase_set_timeout(tc_limits, 15);
    tcase_add_test(tc_limits, test_quickjs_binding_allocation_failure);
    tcase_add_test(tc_limits, test_quickjs_predicate_helper);
    tcase_add_test(tc_limits, test_quickjs_timeout_recovery);
    tcase_add_test(tc_limits, test_quickjs_microtask_timeout);
    tcase_add_test(tc_limits, test_quickjs_microtask_shared_budget);
    tcase_add_test(tc_limits, test_quickjs_exception_diagnostics);
    tcase_add_test(tc_limits, test_quickjs_memory_stack_limits);
    tcase_add_test(tc_limits, test_quickjs_disabled_timeout);
    suite_add_tcase(s, tc_limits);

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
