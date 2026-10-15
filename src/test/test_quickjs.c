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
#include <wisp/utils/corestrings.h>
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
    ck_assert_int_eq(corestrings_init(), NSERROR_OK);
    memset(&dom_html, 0, sizeof(dom_html));
    const char *source = "<!doctype html><html><head><title>Example</title></head><body>"
                         "<div id='target' class='old'>A<span>B</span><!--ignored--><i></i>C</div>"
                         "<p id='duplicate'>first</p><p id='duplicate'>second</p><p id=''></p>"
                         "<svg xmlns='http://www.w3.org/2000/svg' id='svg'><text id='svgText'>svg</text>"
                         "<linearGradient id='gradient'></linearGradient></svg></body></html>";
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
    ck_assert_int_eq(corestrings_fini(), NSERROR_OK);
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
        "document.querySelector === undefined && document.createElementNS === undefined",
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

static const struct {
    const char *context, *markup, *expected;
} inner_html_cases[] = {
    {"div", "<B id=X>A&amp;&lt;</B>ș😀", "<b id=\"X\">A&amp;&lt;</b>ș😀"},
    {"table", "<tr><td>A<td>B", "<tbody><tr><td>A</td><td>B</td></tr></tbody>"},
    {"tbody", "<tr><td>A", "<tr><td>A</td></tr>"},
    {"tr", "<td>A", "<td>A</td>"},
    {"select", "<option>A<option>B", "<option>A</option><option>B</option>"},
    {"textarea", "<b>&amp;</b>", "&lt;b&gt;&amp;&lt;/b&gt;"},
    {"script", "if (x < y && z > 0) alert('&')", "if (x < y && z > 0) alert('&')"},
    {"noscript", "<b>&amp;</b>", "<b>&amp;</b>"},
    {"div", "<p>A<p>B<!--C--><br>", "<p>A</p><p>B<!--C--><br></p>"},
    {"div", "<svg><linearGradient/><text>SVG</text></svg>",
        "<svg><linearGradient></linearGradient><text>SVG</text></svg>"},
    {"template", "<tr><td>A", "<tr><td>A</td></tr>"},
    {"template", "<template><b>A</b></template>", "<template><b>A</b></template>"},
    {"template", "<noscript><b>A</b></noscript>", "<noscript>&lt;b&gt;A&lt;/b&gt;</noscript>"},
};

START_TEST(test_quickjs_inner_html_contexts)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "context", JS_NewString(dom_thread->ctx, inner_html_cases[_i].context));
    JS_SetPropertyStr(dom_thread->ctx, global, "markup", JS_NewString(dom_thread->ctx, inner_html_cases[_i].markup));
    JS_SetPropertyStr(dom_thread->ctx, global, "expected", JS_NewString(dom_thread->ctx, inner_html_cases[_i].expected));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert_msg(exec_script(dom_thread,
        "var el=document.createElement(context);el.innerHTML=markup;"
        "if(el.innerHTML!==expected)throw Error('got '+el.innerHTML+' expected '+expected);"
        "if(el.parentNode!==null)throw Error('attached context');"
        "if(context==='template'&&(el.firstChild!==null||el.textContent!==''))throw Error('template boundary');"
        "if(context!=='template'&&el.firstChild.ownerDocument!==document)throw Error('wrong owner');",
        "innerHTML contexts"), "context case %d (%s)", _i, inner_html_cases[_i].context);
}
END_TEST

START_TEST(test_quickjs_inner_html_identity)
{
    ck_assert(exec_script(dom_thread,
        "var el=document.getElementById('target'), old=el.children[0], live=el.childNodes, tags=el.getElementsByTagName('*');"
        "old.expando=42;old.addEventListener('kept',()=>{old.expando++;});"
        "el.innerHTML='<b id=new-child>New</b><i>Tail</i>';"
        "if(el.childNodes!==live||live.length!==2||tags.length!==2||live[0]!==document.getElementById('new-child'))"
        "throw Error('live identity');"
        "if(old.parentNode!==null||old.isConnected||old.ownerDocument!==document||old.textContent!=='B')throw Error('old subtree');"
        "old.dispatchEvent(new Event('kept'));if(old.expando!==43)throw Error('old listeners');"
        "var fresh=el.firstChild;el.innerHTML=el.innerHTML;"
        "if(el.firstChild===fresh||fresh.parentNode!==null)throw Error('replacement by value');"
        "var svg=document.getElementById('svg');svg.innerHTML='<linearGradient/><text>New</text>';"
        "if(svg.children[0].tagName!=='linearGradient'||svg.children[0] instanceof HTMLElement||"
        "svg.innerHTML!=='<linearGradient></linearGradient><text>New</text>')throw Error('SVG context');"
        "var f=document.createElement('form'), holder=document.createElement('div');f.appendChild(holder);"
        "holder.innerHTML='<form><input name=x></form>';if(holder.children.length!==1||holder.firstChild.tagName!=='INPUT')"
        "throw Error('form context');"
        "var input=document.createElement('input');input.innerHTML='hidden DOM child';"
        "if(input.textContent!=='hidden DOM child'||input.innerHTML!=='')throw Error('void serialization');"
        "el.innerHTML=null;if(live.length!==0||tags.length!==0)throw Error('empty live lists');",
        "innerHTML replacement and identity"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "old.expando===43&&old.textContent==='B'&&fresh.textContent==='New'", "retained nodes after GC");
}
END_TEST

START_TEST(test_quickjs_inner_html_conversion)
{
    ck_assert(exec_script(dom_thread,
        "var el=document.getElementById('target'), desc=Object.getOwnPropertyDescriptor(Element.prototype,'innerHTML');"
        "if(!desc.enumerable||!desc.configurable||typeof desc.get!=='function'||typeof desc.set!=='function')throw Error('descriptor');"
        "var original=el.innerHTML, first=el.firstChild, marker={}, converted=false;"
        "for(var value of [Symbol(),{toString(){throw marker;}}]){"
        "let caught=false;try{el.innerHTML=value;}catch(e){caught=e===marker||e instanceof TypeError;}"
        "if(!caught||el.innerHTML!==original||el.firstChild!==first)throw Error('failed conversion mutated');}"
        "for(var receiver of [document, document.createTextNode('x'), document.createDocumentFragment(),{}]){"
        "for(var action of [()=>desc.get.call(receiver),()=>desc.set.call(receiver,{toString(){converted=true;return '';}})]){"
        "let caught=false;try{action();}catch(e){caught=e instanceof TypeError;}if(!caught)throw Error('brand');}}"
        "if(converted)throw Error('converted wrong receiver');"
        "el.innerHTML=undefined;if(el.innerHTML!=='undefined')throw Error('undefined');"
        "el.innerHTML=null;if(el.firstChild!==null||el.innerHTML!=='')throw Error('null');"
        "el.innerHTML={toString(){el.innerHTML='<i>intermediate</i>';return '<b>final</b>';}};"
        "if(el.innerHTML!=='<b>final</b>')throw Error('reentrant conversion');"
        "el.innerHTML='A\\0B';if(el.textContent!=='AB')throw Error('HTML null handling');",
        "innerHTML Web IDL conversion"));
}
END_TEST

START_TEST(test_quickjs_inner_html_template)
{
    ck_assert(exec_script(dom_thread,
        "var t=document.createElement('template');document.body.appendChild(t);"
        "t.textContent='ordinary child';t.innerHTML='<b id=inert>A</b><template><i>B</i></template>';"
        "if(t.textContent!=='ordinary child'||t.childNodes.length!==1||t.getElementsByTagName('*').length!==0||"
        "document.getElementById('inert')!==null)throw Error('template contents leaked');"
        "if(t.innerHTML!=='<b id=\"inert\">A</b><template><i>B</i></template>')throw Error('template serialization');",
        "innerHTML template boundaries"));
    JS_RunGC(dom_heap->rt);
    /* Direct evaluation leaves pending invalidation observable. Only contents
     * change; the connected template's ordinary rendered children do not. */
    dom_thread->dom_changes = 0;
    const char *source = "t.innerHTML='<p>C</p>'; t.innerHTML";
    JSValue result = JS_Eval(dom_thread->ctx, source, strlen(source), "inert replacement", JS_EVAL_TYPE_GLOBAL);
    ck_assert(!JS_IsException(result));
    JS_FreeValue(dom_thread->ctx, result);
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
    assert_predicate(dom_thread, "t.innerHTML==='<p>C</p>'&&t.textContent==='ordinary child'", "template replacement survives GC");
}
END_TEST

START_TEST(test_quickjs_inner_html_large)
{
    ck_assert(exec_script(dom_thread,
        "var el=document.createElement('div'), unit='<span title=\"&lt;&amp;&gt;\">ș😀</span>';"
        "el.innerHTML=unit.repeat(1200);if(el.children.length!==1200||el.innerHTML!==unit.repeat(1200))throw Error('chunking');"
        "el.innerHTML='';if(el.childNodes.length!==0)throw Error('large clear');",
        "innerHTML multi-chunk parsing"));
}
END_TEST

START_TEST(test_quickjs_inner_html_allocation_failure)
{
    ck_assert(exec_script(dom_thread,
        "var el=document.createElement('div');el.innerHTML='<b title=\"x\">text</b>';"
        "var readMarkup=()=>el.innerHTML;", "innerHTML allocation fixture"));
    JSContext *ctx = dom_thread->ctx;
    JSValue global = JS_GetGlobalObject(ctx), fn = JS_GetPropertyStr(ctx, global, "readMarkup");
    bool failed = false, succeeded = false;
    for (size_t extra = 0; extra <= 8192; extra += 128) {
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(dom_heap->rt, &usage);
        JS_SetMemoryLimit(dom_heap->rt, extra == 0 ? 1 : usage.malloc_size + extra);
        JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 0, NULL);
        JS_SetMemoryLimit(dom_heap->rt, 0);
        if (JS_IsException(result)) {
            failed = true;
            JS_FreeValue(ctx, JS_GetException(ctx));
        } else {
            succeeded = true;
        }
        JS_FreeValue(ctx, result);
        if (succeeded)
            break;
    }
    ck_assert(failed && succeeded);
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    assert_predicate(dom_thread, "el.innerHTML==='<b title=\"x\">text</b>'", "serialization OOM recovery");
}
END_TEST

static JSValue expire_markup_execution(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    thread->heap->deadline_ms = 0;
    return JS_UNDEFINED;
}

START_TEST(test_quickjs_inner_html_execution_limit)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "expire", JS_NewCFunction(dom_thread->ctx, expire_markup_execution, "expire", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(!exec_script(dom_thread,
        "var el=document.getElementById('target');try{el.innerHTML={toString(){expire();return '<b>wrong</b>';}};}"
        "catch(e){globalThis.caughtInterrupt=true;}", "innerHTML shared deadline"));
    assert_predicate(dom_thread, "el.textContent==='ABC'&&typeof caughtInterrupt==='undefined'", "interruption preserves tree and recovers");
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
        assert_predicate(thread, "document.body instanceof HTMLElement && new XMLHttpRequest().readyState === 0 && "
            "document.defaultView === window && "
            "document.body.childNodes instanceof NodeList && document.body.children instanceof HTMLCollection && "
            "document.body.childNodes[0] === document.body.children[0]",
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

/* DOM binding tests report their JS assertion label even when logging is off. */
static bool exec_dom_script(jsthread *thread, const char *script, const char *label)
{
    size_t length = strlen(script) + 150;
    char *wrapped = malloc(length);
    ck_assert_ptr_nonnull(wrapped);
    snprintf(wrapped, length, "try{%s}catch(e){globalThis.domTestFailure=String(e);throw e}", script);
    bool ok = exec_script(thread, wrapped, label);
    free(wrapped);
    if (!ok) {
        JSValue global = JS_GetGlobalObject(thread->ctx);
        JSValue reason = JS_GetPropertyStr(thread->ctx, global, "domTestFailure");
        const char *text = JS_ToCString(thread->ctx, reason);
        fprintf(stderr, "%s: %s\n", label, text != NULL ? text : "evaluation failed");
        JS_FreeCString(thread->ctx, text);
        JS_FreeValue(thread->ctx, reason);
        JS_FreeValue(thread->ctx, global);
    }
    return ok;
}

/* https://dom.spec.whatwg.org/#dom-node-contains */
START_TEST(test_quickjs_node_contains)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "function typeError(fn){try{fn()}catch(e){check(e instanceof TypeError,'exception type');return}throw Error('no TypeError')}"
        "var root=document.getElementById('target'), text=root.firstChild, sibling=root.nextSibling;"
        "check(Node.prototype.contains.length===1,'arity');"
        "check(root.contains(root) && root.contains(text) && document.contains(text),'inclusive ancestors');"
        "check(!text.contains(root) && !root.contains(sibling) && !sibling.contains(text),'other branches');"
        "check(document.contains(document) && document.contains(document.firstChild),'document and doctype');"
        "check(!root.contains(null) && !root.contains(undefined),'nullable argument');"
        "typeError(()=>root.contains());"
        "for(var x of [true,0,'x',{},Object.create(Node.prototype),new Proxy(root,{})]) {"
        "typeError(()=>root.contains(x));typeError(()=>Node.prototype.contains.call(x,root))}"
        "typeError(()=>Node.prototype.contains.call(null,null));"
        "typeError(()=>root.contains({valueOf(){throw Error('must not convert')}}));"
        "var detached=root.cloneNode(true);"
        "check(detached.contains(detached.firstChild) && !document.contains(detached),'detached subtree');"
        "var fragment=document.createDocumentFragment();fragment.appendChild(detached);"
        "check(fragment.contains(detached.firstChild) && !detached.contains(fragment),'fragment ancestry');"
        "var copy=document.cloneNode(true);"
        "check(copy.contains(copy.body) && !document.contains(copy.body) && !copy.contains(text),'separate document');"
        "copy.body.appendChild(detached);"
        "check(copy.contains(detached.firstChild) && !fragment.contains(detached),'adoption updates containment');"
        "copy.body.removeChild(detached);"
        "check(!copy.contains(detached) && detached.contains(detached.firstChild),'removal updates containment');"
        "check(document.getElementById('svg').contains(document.getElementById('svgText')),'SVG ancestry');"
        /* The site's Sizzle branch filters a native tag query through contains. */
        "var list=document.createElement('ul');list.innerHTML='<li><img></li>';"
        "check(Array.from(list.getElementsByTagName('img')).filter(n=>list.contains(n.parentNode)).length===1,'gallery filter');",
        "Node.contains tree relationships and Web IDL"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

/* https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-document-defaultview */
START_TEST(test_quickjs_document_default_view)
{
    ck_assert(exec_dom_script(dom_thread,
        "function check(ok,label){if(!ok)throw Error(label)}"
        "var descriptor=Object.getOwnPropertyDescriptor(Document.prototype,'defaultView');"
        "check(descriptor.enumerable && descriptor.configurable && descriptor.set===undefined,'readonly descriptor');"
        "check(document.defaultView===window && document.body.ownerDocument.defaultView===globalThis,'associated window');"
        "check(document.defaultView.document===document && document.defaultView.getComputedStyle===getComputedStyle,'window identity');"
        "var copy=document.cloneNode(true), empty=document.cloneNode();"
        "check(copy.defaultView===null && empty.defaultView===null && copy.body.ownerDocument.defaultView===null,'no browsing context');"
        "copy.body.appendChild(document.createElement('span'));"
        "check(copy.body.lastChild.ownerDocument.defaultView===null,'adopted node uses destination document');"
        "document.defaultView=null;check(document.defaultView===window,'readonly assignment');"
        "for(var bad of [null,{},document.body,Object.create(Document.prototype)]) {"
        "try{descriptor.get.call(bad);throw Error('bad receiver accepted')}catch(e){check(e instanceof TypeError,'receiver brand')}}"
        "var saved=globalThis;globalThis={};check(document.defaultView===saved,'not a mutable global lookup');saved.globalThis=saved;",
        "Document.defaultView browsing-context association"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

START_TEST(test_quickjs_node_context_retirement)
{
    JSContext *ctx = dom_thread->ctx;
    JSValue global = JS_GetGlobalObject(ctx), doc = JS_GetPropertyStr(ctx, global, "document");
    JSValue contains = JS_GetPropertyStr(ctx, doc, "contains");
    js_closethread(dom_thread);
    for (unsigned i = 0; i < 2; i++) {
        JSValue result = i == 0 ? JS_GetPropertyStr(ctx, doc, "defaultView") : JS_Call(ctx, contains, doc, 1, &doc);
        ck_assert_msg(JS_IsException(result), "Closed document accepted %s", i ? "contains" : "defaultView");
        JSValue error = JS_GetException(ctx), name = JS_GetPropertyStr(ctx, error, "name");
        const char *text = JS_ToCString(ctx, name);
        ck_assert_str_eq(text, "InvalidStateError");
        JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, name);
        JS_FreeValue(ctx, error);
    }
    JS_FreeValue(ctx, contains);
    JS_FreeValue(ctx, doc);
    JS_FreeValue(ctx, global);
}
END_TEST
#include "test_quickjs_style.inc"
#include "test_quickjs_clone.inc"
#include "test_quickjs_lazy_images.inc"
#include "test_quickjs_view_events.inc"

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

START_TEST(test_quickjs_inner_html_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retire", JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var el=document.getElementById('target');var caught=false;"
        "try{el.innerHTML={toString(){retire();return '<b>wrong</b>';}};}catch(e){caught=e.name==='InvalidStateError';}"
        "if(!caught)throw Error('retired setter');"
        "caught=false;try{el.innerHTML;}catch(e){caught=e.name==='InvalidStateError';}if(!caught)throw Error('retired getter');",
        "innerHTML retirement"));
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

START_TEST(test_quickjs_child_collections)
{
    ck_assert(exec_script(dom_thread,
        "var el = document.getElementById('target'), nodes = el.childNodes, elements = el.children;"
        "if (!(nodes instanceof NodeList) || !(elements instanceof HTMLCollection) || Array.isArray(nodes) || "
        "nodes !== el.childNodes || elements !== el.children) throw Error('collection identity');"
        "if (nodes.length !== 5 || elements.length !== 2 || nodes[0] !== el.firstChild || "
        "nodes[1] !== elements[0] || nodes[2].nodeType !== 8 || nodes[3] !== elements[1]) throw Error('node filtering');"
        "if (nodes.item(5) !== null || nodes[5] !== undefined || elements.item(2) !== null || elements[2] !== undefined) "
        "throw Error('missing entries');"
        "if (document.childNodes.length !== 2 || document.childNodes[0].nodeType !== 10 || "
        "document.children.length !== 1 || document.children[0] !== document.documentElement) throw Error('document children');"
        "if (nodes[0].childNodes.length !== 0 || 'children' in nodes[0] || 'children' in nodes[2]) throw Error('leaf interfaces');"
        "var fragment = document.createDocumentFragment(), f = fragment.children;"
        "fragment.appendChild(document.createTextNode('text')); fragment.appendChild(document.createElement('b'));"
        "if (f !== fragment.children || f.length !== 1 || fragment.childNodes.length !== 2) throw Error('fragment children');"
        "if (Object.prototype.toString.call(nodes) !== '[object NodeList]' || "
        "Object.prototype.toString.call(elements) !== '[object HTMLCollection]') throw Error('tags');",
        "childNodes and children interfaces and filtering"));
}
END_TEST

START_TEST(test_quickjs_collections_live)
{
    ck_assert(exec_script(dom_thread,
        "var el = document.getElementById('target'), n = el.childNodes, c = el.children, first = c[0];"
        "el.insertBefore(document.createComment('before'), n[0]);"
        "if (n.length !== 6 || c.length !== 2 || c[0] !== first) throw Error('live insert');"
        "var f = document.createDocumentFragment(), fc = f.children; f.appendChild(first);"
        "if (c.length !== 1 || fc[0] !== first || first.isConnected) throw Error('move to detached');"
        "el.appendChild(f); if (fc.length !== 0 || c[1] !== first) throw Error('fragment insertion');"
        "el.textContent = 'replacement'; if (n.length !== 1 || n[0].data !== 'replacement' || c.length !== 0) "
        "throw Error('textContent replacement');"
        "var dc = document.children, dn = document.childNodes, root = document.documentElement; document.removeChild(root);"
        "if (dc.length !== 0 || dn.length !== 1 || n.length !== 1) throw Error('root removal');"
        "document.appendChild(root); if (dc[0] !== root || dn.length !== 2) throw Error('root restore');"
        "var detached = document.createElement('div'), live = detached.childNodes;"
        "for (var i = 0; i < 30; i++) detached.appendChild(document.createElement('i'));"
        "while (live.length) detached.removeChild(live[0]);"
        "if (detached.hasChildNodes()) throw Error('live removal loop');",
        "collections reflect tree edits, detached nodes, fragments and root replacement"));
}
END_TEST

START_TEST(test_quickjs_collection_names)
{
    ck_assert(exec_script(dom_thread,
        "var p = document.createElement('div'), a = document.createElement('span'), b = document.createElement('b');"
        "a.id = 'alpha'; a.setAttribute('name', 'shared'); b.id = 'shared'; p.appendChild(a); p.appendChild(b); var c = p.children;"
        "if (c.alpha !== a || c.shared !== a || c.namedItem('shared') !== a || c.namedItem('') !== null || "
        "c.namedItem('absent') !== null || c.absent !== undefined) throw Error('first id-or-name in tree order');"
        "a.removeAttribute('name'); if (c.shared !== b) throw Error('attribute removal');"
        "b.id = 'beta'; if (c.shared !== undefined || 'shared' in c || c.beta !== b) throw Error('live ID');"
        "a.id = 'item'; if (typeof c.item !== 'function' || c.namedItem('item') !== a || "
        "Object.hasOwn(c, 'item')) throw Error('method shadowing');"
        "a.id = '01'; b.id = '99'; if (c['01'] !== a || c[99] !== undefined || c.namedItem('99') !== b) "
        "throw Error('numeric names and indices');"
        "var svg = document.getElementById('svg'), sc = svg.children; sc[0].setAttribute('name', 'foreign');"
        "if (sc.foreign !== undefined || sc.namedItem('foreign') !== null || sc.svgText !== sc[0]) throw Error('SVG names');"
        "a.id = 'ș😀\\0'; if (c.namedItem('ș😀\\0') !== a || c['ș😀\\0'] !== a) throw Error('length-aware names');",
        "HTMLCollection ID/name lookup and namespace rules"));
}
END_TEST

START_TEST(test_quickjs_collection_properties)
{
    ck_assert(exec_script(dom_thread,
        "var p = document.createElement('div'), a = document.createElement('span'), b = document.createElement('b');"
        "a.id = 'alpha'; a.setAttribute('name', 'alias'); b.id = 'beta'; b.setAttribute('name', 'alpha');"
        "p.appendChild(a); p.appendChild(b); var c = p.children, n = p.childNodes, sym = Symbol('note');"
        "c.note = 7; c[sym] = 8;"
        "var k = Reflect.ownKeys(c); if (k.length !== 7 || k.slice(0,6).join() !== '0,1,alpha,alias,beta,note' || k[6] !== sym) "
        "throw Error('index/name/string/symbol ordering');"
        "if (Object.keys(c).join() !== '0,1,note') throw Error('named properties must be unenumerable');"
        "for (const list of [n,c]) { const d = Object.getOwnPropertyDescriptor(list,'0');"
        "if (!d.enumerable || !d.configurable || d.writable || d.value !== a) throw Error('index descriptor');"
        "if (Reflect.set(list,'0',9) || Reflect.set(list,'99',9) || Reflect.defineProperty(list,'99',{value:9}) || "
        "Reflect.deleteProperty(list,'0') || !Reflect.deleteProperty(list,'99')) throw Error('readonly indices');"
        "let caught; try { (function(){'use strict';list[99]=9;})(); } catch(e){caught=e;}"
        "if (!(caught instanceof TypeError)) throw Error('strict indexed assignment');"
        "list['01']=6; list['4294967295']=7; if (list['01']!==6 || list['4294967295']!==7) throw Error('non-indices');"
        "if (Reflect.preventExtensions(list) || !Object.isExtensible(list)) throw Error('extensibility'); }"
        "const d = Object.getOwnPropertyDescriptor(c,'alpha');"
        "if (d.enumerable || d.writable || !d.configurable || d.value !== a) throw Error('named descriptor');"
        "if (Reflect.set(c,'alpha',4) || Reflect.defineProperty(c,'alpha',{value:4}) || Reflect.deleteProperty(c,'alpha')) "
        "throw Error('readonly names');"
        "c.future = 10; b.id = 'future'; if (c.future !== 10 || c.namedItem('future') !== b) throw Error('own expando priority');"
        "if (Reflect.ownKeys(c).filter(x => x === 'future').length !== 1) throw Error('duplicate name');"
        "delete c.future; if (c.future !== b) throw Error('unhide name');"
        "Object.defineProperty(c,'locked',{value:42});"
        "if (Reflect.deleteProperty(c,'locked') || Reflect.set(c,'locked',43)) throw Error('expando descriptors');",
        "collection descriptors, own keys and writes"));
}
END_TEST

START_TEST(test_quickjs_collection_prototypes)
{
    ck_assert(exec_script(dom_thread,
        "var p = document.createElement('div'), a = document.createElement('span'); a.id='named'; p.appendChild(a); var c=p.children;"
        "var proto = Object.create(HTMLCollection.prototype), receiver, assigned;"
        "Object.defineProperty(proto,'named',{get(){throw Error('getter must not run');},set(v){receiver=this;assigned=v;},configurable:true});"
        "Object.setPrototypeOf(c,proto);"
        "if (!Reflect.set(c,'named',8) || receiver!==c || assigned!==8 || Object.hasOwn(c,'named')) throw Error('prototype setter');"
        "delete proto.named; var derived=Object.create(c);"
        "if (!Reflect.set(c,'named',9,derived) || derived.named!==9 || c.named!==a) throw Error('different receiver');"
        "if (Reflect.set(c,'0',9,derived) || Object.hasOwn(derived,'0')) throw Error('indexed different receiver');"
        "var called=false; Object.setPrototypeOf(c,new Proxy(HTMLCollection.prototype,{"
        "getOwnPropertyDescriptor(t,k){return Reflect.getOwnPropertyDescriptor(t,k);},"
        "set(t,k,v,r){called = k==='named' && v===10 && r===c;return true;}}));"
        "if (!Reflect.set(c,'named',10) || !called) throw Error('prototype Proxy set');",
        "named lookup and assignment respect ordinary prototype/receiver semantics"));
}
END_TEST

START_TEST(test_quickjs_collection_iteration)
{
    ck_assert(exec_script(dom_thread,
        "var p=document.createElement('div'), a=document.createElement('a'), b=document.createElement('b');"
        "p.appendChild(a);p.appendChild(b);var n=p.childNodes,c=p.children;"
        "if (n.values!==Array.prototype.values || n[Symbol.iterator]!==n.values || n.forEach!==Array.prototype.forEach || "
        "c[Symbol.iterator]!==Array.prototype.values || 'values' in c || 'forEach' in c) throw Error('iterator interfaces');"
        "if ([...n][0]!==a || [...c][1]!==b || [...n.keys()].join()!=='0,1' || [...n.entries()][1][1]!==b) throw Error('iteration');"
        "var iterator=n.values(); if (iterator.next().value!==a) throw Error('iterator first');"
        "p.removeChild(b); var text=document.createTextNode('next');p.appendChild(text);"
        "if (iterator.next().value!==text || !iterator.next().done) throw Error('live iterator');"
        "p.appendChild(b);if (!iterator.next().done) throw Error('finished iterator');"
        "var seen=[], marker={};n.forEach(function(value,index,list){if(this!==marker||list!==n)throw Error('callback');"
        "seen.push(value);if(index===0){p.removeChild(text);p.appendChild(document.createTextNode('last'));}},marker);"
        "if (seen.length!==3 || seen[0]!==a || seen[1]!==b || seen[2].data!=='last') throw Error('live forEach contents');",
        "standard live array iterators and forEach"));
}
END_TEST

START_TEST(test_quickjs_collection_errors)
{
    ck_assert(exec_script(dom_thread,
        "var p=document.getElementById('target'),n=p.childNodes,c=p.children;"
        "function raises(fn){try{fn();}catch(e){if(e instanceof TypeError)return;throw e;}throw Error('missing TypeError');}"
        "raises(()=>new NodeList());raises(()=>new HTMLCollection());raises(()=>n.item());raises(()=>c.namedItem());"
        "raises(()=>n.item(Symbol()));raises(()=>n.item(0n));raises(()=>c.namedItem(Symbol()));"
        "raises(()=>NodeList.prototype.item.call(c,0));raises(()=>HTMLCollection.prototype.item.call(n,0));"
        "raises(()=>Object.getOwnPropertyDescriptor(HTMLCollection.prototype,'length').get.call(n));"
        "raises(()=>Object.getOwnPropertyDescriptor(Element.prototype,'children').get.call(p.firstChild));"
        "raises(()=>Object.getOwnPropertyDescriptor(Element.prototype,'children').get.call(document));"
        "if(n.item(4294967296)!==n[0]||n.item(-1)!==null||n.item(NaN)!==n[0])throw Error('index conversion');"
        "var sentinel={},caught;try{c.namedItem({toString(){throw sentinel;}});}catch(e){caught=e;}"
        "if(caught!==sentinel)throw Error('original coercion exception');"
        "var found=n.item({valueOf(){p.textContent='converted';return 0;}});"
        "if(found.data!=='converted'||n.length!==1)throw Error('reentrant index conversion');"
        "p.appendChild(document.createElement('i'));"
        "if(c.namedItem({toString(){c[0].id='converted';return 'converted';}})!==c[0])throw Error('reentrant name conversion');",
        "collection Web IDL conversion, arity and receiver validation"));
}
END_TEST

START_TEST(test_quickjs_collection_retirement)
{
    ck_assert(exec_script(dom_thread,
        "var p=document.createElement('div');p.appendChild(document.createElement('b'));"
        "var n=p.childNodes,c=p.children; n.cycle={list:c,root:p};c.cycle=n;p=null;",
        "collection cycles and detached lifetime"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "n.length===1 && n[0]===c[0]", "GC retains detached collection root");
    js_closethread(dom_thread);
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue list = JS_GetPropertyStr(dom_thread->ctx, global, "n");
    JSValue length = JS_GetPropertyStr(dom_thread->ctx, list, "length");
    ck_assert(JS_IsException(length));
    JS_FreeValue(dom_thread->ctx, JS_GetException(dom_thread->ctx));
    JS_FreeValue(dom_thread->ctx, list);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

START_TEST(test_quickjs_collection_allocation_failure)
{
    /* Sweep property enumeration after successful setup; its atom table and
     * expando/named-key merging must unwind without poisoning the live list. */
    ck_assert(exec_script(dom_thread,
        "var p=document.createElement('div');"
        "for(var i=0;i<20;i++){var c=document.createElement('b');c.id='id'+i;c.setAttribute('name','name'+i);p.appendChild(c);}"
        "var list=p.children;list.note=42;list[Symbol('s')]=7;"
        "var enumerate=()=>Reflect.ownKeys(list);",
        "collection allocation fixture"));
    if (_i != 0)
        ck_assert(exec_script(dom_thread, "list=p.getElementsByTagName('*');list.note=42;list[Symbol('s')]=7;",
            "query allocation fixture"));
    JSContext *ctx = dom_thread->ctx;
    JSValue global = JS_GetGlobalObject(ctx), fn = JS_GetPropertyStr(ctx, global, "enumerate");
    bool failed = false, succeeded = false;
    JS_RunGC(dom_heap->rt);
    for (size_t extra = 0; extra <= 65536; extra += 256) {
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(dom_heap->rt, &usage);
        JS_SetMemoryLimit(dom_heap->rt, extra == 0 ? 1 : usage.malloc_size + extra);
        JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 0, NULL);
        JS_SetMemoryLimit(dom_heap->rt, 0);
        if (JS_IsException(result)) {
            failed = true;
            JS_FreeValue(ctx, JS_GetException(ctx));
        } else {
            succeeded = true;
            ck_assert(!JS_HasException(ctx));
        }
        JS_FreeValue(ctx, result);
        if (succeeded)
            break;
    }
    ck_assert_msg(failed && succeeded, "Collection allocation sweep: failed=%d succeeded=%d", failed, succeeded);
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    assert_predicate(
        dom_thread, "Reflect.ownKeys(list).length === 62 && list.id19 === list[19]", "enumeration OOM recovery");
}
END_TEST

START_TEST(test_quickjs_collection_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    ck_assert_int_eq(JS_SetPropertyStr(dom_thread->ctx, global, "retire",
                         JS_NewCFunction(dom_thread->ctx, close_dom_during_conversion, "retire", 0)),
        1);
    JS_FreeValue(dom_thread->ctx, global);
    const char *sources[] = {
        "var n=document.body.childNodes;try{n.item({valueOf(){retire();return 0;}});}catch(e){var caught=e;}",
        "var n=document.body.children;try{n.namedItem({toString(){retire();return 'target';}});}catch(e){var caught=e;}",
        "var n=document.body.children;Object.setPrototypeOf(n,new Proxy({}, {getOwnPropertyDescriptor(){retire();}}));"
        "try{n.target;}catch(e){var caught=e;}",
    };
    char script[1024];
    snprintf(script, sizeof(script),
        "%s if (!(caught instanceof DOMException) || caught.name !== 'InvalidStateError') "
        "throw Error('retired collection guard');",
        sources[_i]);
    ck_assert_msg(exec_script(dom_thread, script, "collection retirement during conversion/prototype lookup"),
        "Retirement variant %d failed", _i);
}
END_TEST

START_TEST(test_quickjs_tag_queries)
{
    ck_assert(exec_script(dom_thread,
        "var p=document.getElementById('target'), all=p.getElementsByTagName('*'), spans=document.getElementsByTagName('SPAN');"
        "if(!(all instanceof HTMLCollection)||all instanceof NodeList||all.length!==2||all[0]!==spans[0]||"
        "all.item(2)!==null||all[2]!==undefined||all.item(-1)!==null)throw Error('query collection');"
        "if(document.getElementsByTagName('html')[0]!==document.documentElement||p.getElementsByTagName('div').length)"
        "throw Error('descendants exclude receiver');"
        "if(document.getElementsByTagName('text')[0]!==document.getElementById('svgText')||"
        "document.getElementsByTagName('TEXT').length)throw Error('SVG tag case');"
        "var ns='http://www.w3.org/1999/xhtml', svg='http://www.w3.org/2000/svg';"
        "if(document.getElementsByTagNameNS(ns,'SPAN').length||document.getElementsByTagNameNS(ns,'span')[0]!==spans[0]||"
        "document.getElementsByTagNameNS(ns.toUpperCase(),'*').length||document.getElementsByTagNameNS(null,'*').length||"
        "document.getElementsByTagNameNS('','*').length||document.getElementsByTagNameNS(undefined,'*').length||"
        "document.getElementsByTagNameNS(svg,'text')[0]!==document.getElementById('svgText')||"
        "document.getElementsByTagNameNS('*','*').length!==document.getElementsByTagName('*').length)"
        "throw Error('namespace query');"
        "if('getElementsByTagName' in document.createDocumentFragment()||'getElementsByTagName' in p.firstChild)"
        "throw Error('query interfaces');",
        "tag queries return live HTMLCollection with HTML/foreign namespace matching"));
    ck_assert_uint_eq(dom_thread->dom_changes, 0);
}
END_TEST

START_TEST(test_quickjs_tag_queries_live)
{
    ck_assert(exec_script(dom_thread,
        "var root=document.createElement('main'), branch=document.createElement('div'), a=document.createElement('span');"
        "var c=root.getElementsByTagName('span'), n=root.getElementsByTagNameNS('*','span');"
        "root.appendChild(branch);branch.appendChild(a);a.id='alpha';a.setAttribute('name','alias');"
        "if(c.length!==1||c[0]!==a||n[0]!==a||c.alpha!==a||c.namedItem('alias')!==a)throw Error('deep live insert');"
        "var b=document.createElement('span');b.id='beta';root.insertBefore(b,branch);"
        "if([...c][0]!==b||[...n][1]!==a||Reflect.ownKeys(c).join()!=='0,1,beta,alpha,alias'||"
        "Object.keys(c).join()!=='0,1')throw Error('tree order and names');"
        "var iterator=c[Symbol.iterator]();if(iterator.next().value!==b)throw Error('iterator first');"
        "branch.removeChild(a);if(c.length!==1||!iterator.next().done||c.alpha!==undefined)throw Error('live remove');"
        "root.appendChild(a);a.id='item';a.removeAttribute('name');"
        "if(typeof c.item!=='function'||c.namedItem('item')!==a||Reflect.ownKeys(c).includes('item'))throw Error('method shadow');"
        "var d=Object.getOwnPropertyDescriptor(c,'0');if(d.writable||!d.enumerable||!d.configurable||d.value!==b)"
        "throw Error('query index descriptor');"
        "if(Reflect.set(c,'0',3)||Reflect.deleteProperty(c,'0')||Reflect.defineProperty(c,'99',{value:2}))"
        "throw Error('readonly indices');c.note=42;if(c.note!==42)throw Error('query expando');"
        "document.body.appendChild(root);document.body.removeChild(root);if(c.length!==2)throw Error('detached query');"
        "root.textContent='gone';if(c.length||n.length||c.namedItem('beta')!==null)throw Error('replacement');",
        "query collections follow deep edits, detached roots, names and live iteration"));
}
END_TEST

START_TEST(test_quickjs_tag_queries_conversion)
{
    ck_assert(exec_script(dom_thread,
        "function raises(fn){try{fn();}catch(e){if(e instanceof TypeError)return;throw e;}throw Error('missing TypeError');}"
        "raises(()=>document.getElementsByTagName());raises(()=>document.getElementsByTagNameNS('*'));"
        "raises(()=>document.getElementsByTagName(Symbol()));raises(()=>document.getElementsByTagNameNS(Symbol(),'a'));"
        "raises(()=>document.getElementsByTagNameNS(null,Symbol()));"
        "raises(()=>Document.prototype.getElementsByTagName.call(document.body,'div'));"
        "raises(()=>Element.prototype.getElementsByTagName.call(document,'div'));"
        "raises(()=>Element.prototype.getElementsByTagName.call(document.createTextNode(''),'div'));"
        "var root=document.createElement('div'), order=[], sentinel={},caught;"
        "var c=root.getElementsByTagNameNS({toString(){order.push('namespace');return '*';}},"
        "{toString(){order.push('name');root.appendChild(document.createElement('b'));return 'b';}});"
        "if(order.join()!=='namespace,name'||c.length!==1)throw Error('ordered conversion');"
        "try{root.getElementsByTagNameNS({toString(){throw sentinel;}},{toString(){throw Error('unexpected conversion');}});}"
        "catch(e){caught=e;}if(caught!==sentinel)throw Error('coercion exception');"
        "if(root.getElementsByTagName(null).length||root.getElementsByTagName(undefined).length||"
        "root.getElementsByTagName(42n).length||root.getElementsByTagName('b\\0').length)throw Error('DOMString conversion');",
        "query receiver checks, arity, DOMString conversion and namespace conversion order"));
}
END_TEST

START_TEST(test_quickjs_tag_queries_retirement)
{
    JSContext *ctx = dom_thread->ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    ck_assert_int_eq(
        JS_SetPropertyStr(ctx, global, "retire", JS_NewCFunction(ctx, close_dom_during_conversion, "retire", 0)), 1);
    JS_FreeValue(ctx, global);
    const char *sources[] = {
        "try{document.getElementsByTagName({toString(){retire();return '*';}});}catch(e){var caught=e;}",
        "try{document.getElementsByTagNameNS('*',{toString(){retire();return '*';}});}catch(e){var caught=e;}",
        "var list=document.getElementsByTagName('*');try{list.item({valueOf(){retire();return 0;}});}catch(e){var caught=e;}",
        "var list=document.getElementsByTagName('*');Object.setPrototypeOf(list,new Proxy({},"
        "{getOwnPropertyDescriptor(){retire();}}));try{list.target;}catch(e){var caught=e;}",
    };
    char script[1024];
    snprintf(script, sizeof(script),
        "%s if(!(caught instanceof DOMException)||caught.name!=='InvalidStateError')"
        "throw Error('retired query guard');",
        sources[_i]);
    ck_assert_msg(exec_script(dom_thread, script, "query retirement during coercion/prototype lookup"),
        "Query retirement variant %d failed", _i);
}
END_TEST

START_TEST(test_quickjs_tag_queries_gc)
{
    ck_assert(exec_script(dom_thread,
        "var root=document.createElement('div');root.appendChild(document.createElement('b'));"
        "var list=root.getElementsByTagName('*');list.cycle={root,list};root=null;",
        "query retains a detached root and traces cycles"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "list.length===1 && list[0].tagName==='B'", "query survives GC");
    js_closethread(dom_thread);
    JSValue global = JS_GetGlobalObject(dom_thread->ctx), list = JS_GetPropertyStr(dom_thread->ctx, global, "list");
    JSValue length = JS_GetPropertyStr(dom_thread->ctx, list, "length");
    ck_assert(JS_IsException(length));
    JS_FreeValue(dom_thread->ctx, JS_GetException(dom_thread->ctx));
    JS_FreeValue(dom_thread->ctx, list);
    JS_FreeValue(dom_thread->ctx, global);
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

START_TEST(test_quickjs_event_values)
{
    ck_assert(exec_script(dom_thread,
        "var detail = {}, e = new CustomEvent('ș\\0event', {bubbles:1,cancelable:true,composed:true,detail});"
        "if (!(e instanceof Event && e instanceof CustomEvent) || e.type !== 'ș\\0event' ||"
        "!e.bubbles || !e.cancelable || !e.composed || e.detail !== detail || e.isTrusted ||"
        "e.target !== null || e.currentTarget !== null || e.eventPhase !== Event.NONE ||"
        "e.composedPath().length || e.timeStamp < 0) throw Error('initial state');"
        "if (Object.prototype.toString.call(e) !== '[object CustomEvent]' || Event.AT_TARGET !== 2 ||"
        "CustomEvent.CAPTURING_PHASE !== 1 || e.BUBBLING_PHASE !== 3) throw Error('interface');"
        "var descriptor = Object.getOwnPropertyDescriptor(e,'isTrusted');"
        "if (descriptor.configurable || !descriptor.enumerable || descriptor.set ||"
        "Reflect.deleteProperty(e, 'isTrusted') || Reflect.set(e,'isTrusted',true)) throw Error('trusted descriptor');"
        "e.preventDefault(); if (!e.defaultPrevented || e.returnValue) throw Error('cancel');"
        "e.returnValue = true; if (!e.defaultPrevented) throw Error('uncancel');"
        "var stamp=e.timeStamp; e.initEvent('reset');"
        "if (e.defaultPrevented || e.bubbles || e.cancelable || e.type!=='reset' || e.timeStamp!==stamp ||"
        "e.detail!==detail || !e.composed) throw Error('initEvent');"
        "e.initCustomEvent('custom',true,true,42); if(e.detail!==42) throw Error('custom init');"
        "e.cancelBubble=true; e.cancelBubble=false; if(!e.cancelBubble) throw Error('unstoppable');"
        "e.initCustomEvent('again'); if(e.detail!==null || e.cancelBubble) throw Error('reset flags');"
        "class Mine extends CustomEvent {} var sub=new Mine('sub');"
        "if(!(sub instanceof Mine) || sub.detail!==null) throw Error('subclass');"
        "var plain=new Event('x'); plain.preventDefault(); if(plain.defaultPrevented) throw Error('uncancelable');",
        "event-values.js"));
}
END_TEST

START_TEST(test_quickjs_event_conversion)
{
    ck_assert(exec_script(dom_thread,
        "var order=[]; new CustomEvent({toString(){order.push('type');return 'x';}}, {"
        "get bubbles(){order.push('b');return 1;}, get cancelable(){order.push('c');return 1;},"
        "get composed(){order.push('o');return 1;}, get detail(){order.push('d');return undefined;}});"
        "if(order.join()!=='type,b,c,o,d') throw Error('dictionary order');"
        "var sentinel={}; try { new Event('x',{get bubbles(){throw sentinel;},"
        "get cancelable(){throw Error('late getter');}}); throw Error('missing throw');}"
        "catch(e){if(e!==sentinel) throw e;}"
        "for(var action of [()=>Event('x'),()=>new Event(),()=>new Event(Symbol()),()=>new Event('x',3),"
        "()=>Event.prototype.preventDefault.call({}),()=>Event.prototype.initEvent.call(new Event('x'))]) {"
        "try{action();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}"
        "var ev=new Event('x'); try {Object.getOwnPropertyDescriptor(CustomEvent.prototype,'detail').get.call(ev);"
        "throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}",
        "event-conversion.js"));
}
END_TEST

START_TEST(test_quickjs_event_dispatch)
{
    ck_assert(exec_script(dom_thread,
        "var t=new EventTarget(), e=new Event('x',{cancelable:true}), order=[];"
        "t.addEventListener('x',function(ev){if(this!==t || ev!==e || ev.target!==t ||"
        "ev.currentTarget!==t || ev.eventPhase!==2 || ev.composedPath()[0]!==t) throw Error('target');"
        "order.push('b'); ev.preventDefault();});"
        "t.addEventListener('x',()=>order.push('c'),true);"
        "if(t.dispatchEvent(e) || order.join()!=='c,b' || !e.defaultPrevented || e.target!==t ||"
        "e.currentTarget!==null || e.eventPhase!==0 || e.composedPath().length) throw Error('dispatch');"
        "e.initEvent('empty'); if(e.target!==null || !t.dispatchEvent(e))throw Error('reuse');"
        "var a=new Event(''); if(!t.dispatchEvent(a))throw Error('empty type');"
        "class Target extends EventTarget {} if(!(new Target() instanceof EventTarget))throw Error('subclass');"
        "if(Object.prototype.toString.call(t)!=='[object EventTarget]')throw Error('tag');",
        "event-dispatch.js"));
}
END_TEST

START_TEST(test_quickjs_event_listener_identity)
{
    ck_assert(exec_script(dom_thread,
        "var t=new EventTarget(), calls=[]; function f(){calls.push('f');}"
        "t.addEventListener('x',f,{once:true});t.addEventListener('x',f,{once:false,passive:true});"
        "t.addEventListener('x',f,true);t.addEventListener('x',()=>calls.push('z'));"
        "t.dispatchEvent(new Event('x'));t.dispatchEvent(new Event('x'));"
        "if(calls.join()!=='f,f,z,f,z')throw Error('duplicate/once');"
        "t.removeEventListener('x',f,{capture:true, get once(){throw Error('remove once');},"
        "get passive(){throw Error('remove passive');},get signal(){throw Error('remove signal');}});"
        "t.dispatchEvent(new Event('x'));if(calls.join()!=='f,f,z,f,z,z')throw Error('remove');"
        "var options=[]; t.addEventListener('none',null,{get capture(){options.push('c');},"
        "get once(){options.push('o');},get passive(){options.push('p');},get signal(){options.push('s');}});"
        "if(options.join()!=='c,o,p,s')throw Error('null callback dictionary conversion');"
        "for(var action of [()=>t.addEventListener('x',1),()=>t.addEventListener('x',f,{signal:{}}),"
        "()=>t.addEventListener('x'),()=>t.dispatchEvent({}),()=>EventTarget.prototype.dispatchEvent.call({},new Event('x'))]) {"
        "try{action();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}",
        "event-listeners.js"));
}
END_TEST

START_TEST(test_quickjs_event_listener_edits)
{
    ck_assert(exec_script(dom_thread,
        "var t=new EventTarget(), calls=[];function next(){calls.push('next');}"
        "function edit(){calls.push('edit');t.removeEventListener('x',edit);"
        "t.removeEventListener('x',next);t.addEventListener('x',next);t.addEventListener('x',()=>calls.push('new'));}"
        "t.addEventListener('x',edit);t.addEventListener('x',next);t.addEventListener('x',()=>calls.push('last'));"
        "t.dispatchEvent(new Event('x'));if(calls.join()!=='edit,last')throw Error('snapshot');"
        "calls=[];t.dispatchEvent(new Event('x'));if(calls.join()!=='last,next,new')throw Error('next invocation');"
        "var t2=new EventTarget(), hits=0;t2.addEventListener('x',function(){hits++;t2.dispatchEvent(new Event('x'));},{once:true});"
        "t2.dispatchEvent(new Event('x'));if(hits!==1)throw Error('once removed too late');"
        "var t3=new EventTarget(), added=0;t3.addEventListener('x',()=>t3.addEventListener('x',()=>added++),true);"
        "t3.dispatchEvent(new Event('x'));if(added!==1)throw Error('snapshot per invocation');",
        "event-edits.js"));
}
END_TEST

START_TEST(test_quickjs_event_stop_and_passive)
{
    ck_assert(exec_script(dom_thread,
        "for(var immediate of [false,true]){var t=new EventTarget(),calls=[];"
        "t.addEventListener('x',e=>{calls.push(1);if(immediate)e.stopImmediatePropagation();else e.stopPropagation();},true);"
        "t.addEventListener('x',()=>calls.push(2),true);t.addEventListener('x',()=>calls.push(3));"
        "t.dispatchEvent(new Event('x'));if(calls.join()!==(immediate?'1':'1,2'))throw Error('stop');}"
        "var t=new EventTarget(), e=new Event('x',{cancelable:true});"
        "function passive(e){e.preventDefault();e.returnValue=false;}"
        "t.addEventListener('x',passive,{passive:true});"
        "if(!t.dispatchEvent(e)||e.defaultPrevented)throw Error('passive');"
        "t.removeEventListener('x',passive);t.addEventListener('x',passive);"
        "if(t.dispatchEvent(e)||!e.defaultPrevented)throw Error('active');"
        "var fresh=new Event('y');fresh.stopImmediatePropagation();"
        "var hits=0;t.addEventListener('y',()=>hits++);t.dispatchEvent(fresh);t.dispatchEvent(fresh);"
        "if(hits!==1 || fresh.cancelBubble)throw Error('reset stop');",
        "event-stop.js"));
}
END_TEST

START_TEST(test_quickjs_window_events)
{
    ck_assert(exec_script(dom_thread,
        "var called=0;function f(e){if(this!==window || e.target!==window || e.currentTarget!==window)"
        "throw Error('window receiver');called++;e.preventDefault();}"
        "addEventListener('wheel',f); if(!dispatchEvent(new Event('wheel',{cancelable:true})))throw Error('default passive');"
        "removeEventListener('wheel',f);addEventListener('wheel',f,{passive:false});"
        "if(dispatchEvent(new Event('wheel',{cancelable:true}))||called!==2)throw Error('explicit active');"
        "removeEventListener('wheel',f);if(!dispatchEvent(new Event('wheel')))throw Error('removed');"
        "if(!(window instanceof EventTarget))throw Error('window prototype');",
        "window-events.js"));
}
END_TEST

START_TEST(test_quickjs_event_callback_errors)
{
    ck_assert(exec_script(dom_thread,
        "var t=new EventTarget(),seen=[],obj={handleEvent(e){if(this!==obj)throw Error('object this');seen.push('object');}};"
        "t.addEventListener('x',()=>{throw new Error('Expected listener error');});"
        "t.addEventListener('x',{});t.addEventListener('x',{get handleEvent(){throw Error('Expected getter error');}});"
        "t.addEventListener('x',obj);t.dispatchEvent(new Event('x'));"
        "obj.handleEvent=function(){seen.push('changed');};t.dispatchEvent(new Event('x'));"
        "if(seen.join()!=='object,changed')throw Error('exception isolation/object invocation');"
        "var nested=new Event('nested');t.addEventListener('nested',e=>{try{t.dispatchEvent(e);throw Error('redispatch');}"
        "catch(error){if(error.name!=='InvalidStateError')throw error;}e.initEvent('wrong');"
        "if(e.type!=='nested')throw Error('in-flight init');seen.push('nested');});"
        "t.dispatchEvent(nested);if(seen[2]!=='nested')throw Error('recursive rejection');",
        "event-callback-errors.js"));
}
END_TEST

START_TEST(test_quickjs_event_microtasks)
{
    ck_assert(exec_script(dom_thread,
        "var t=new EventTarget(),order=[];t.addEventListener('x',()=>{order.push(1);queueMicrotask(()=>order.push(4));});"
        "t.addEventListener('x',()=>order.push(2));t.dispatchEvent(new Event('x'));order.push(3);",
        "event-microtasks.js"));
    assert_predicate(dom_thread, "order.join()==='1,2,3,4'", "script-dispatched callback checkpoint");
}
END_TEST

START_TEST(test_quickjs_event_timeout)
{
    ck_assert(!exec_script(dom_thread,
        "var t=new EventTarget(),late=false,caught=false,after=false,job=false,ev=new Event('x');"
        "t.addEventListener('x',()=>{queueMicrotask(()=>job=true);while(true){};});"
        "t.addEventListener('x',()=>late=true);try{t.dispatchEvent(ev);}catch(e){caught=true;}after=true;",
        "event-timeout.js"));
    assert_predicate(dom_thread, "!late&&!caught&&!after&&!job&&ev.currentTarget===null&&ev.eventPhase===0",
        "event interrupt cleanup");
    assert_predicate(dom_thread, "new EventTarget().dispatchEvent(new Event('ok'))", "recovery after event interrupt");
}
END_TEST

START_TEST(test_quickjs_event_gc)
{
    JSMemoryUsage before, after;
    ck_assert(exec_script(dom_thread,
        "var savedEvent=new Event('saved'); {let target=new EventTarget();target.addEventListener('saved',()=>target);"
        "target.dispatchEvent(savedEvent);}"
        "function cycles(){for(let i=0;i<100;i++){let t=new EventTarget(),event=new CustomEvent('x',{detail:t});"
        "t.addEventListener('x',()=>event);t.dispatchEvent(event);}} cycles();",
        "event-cycles.js"));
    JS_RunGC(dom_heap->rt);
    JS_ComputeMemoryUsage(dom_heap->rt, &before);
    ck_assert(exec_script(dom_thread, "cycles();", "more event cycles"));
    JS_RunGC(dom_heap->rt);
    JS_ComputeMemoryUsage(dom_heap->rt, &after);
    ck_assert_msg(after.obj_count <= before.obj_count + 2, "Event/listener cycles leaked %lld objects",
        (long long)(after.obj_count - before.obj_count));
    assert_predicate(dom_thread, "savedEvent.target.dispatchEvent(savedEvent)", "saved Event retains JS/native target");
}
END_TEST

static JSValue close_from_listener(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    js_closethread(dom_thread);
    return JS_UNDEFINED;
}

START_TEST(test_quickjs_event_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var calls=0;addEventListener('x',()=>{calls++;retireEvents();});addEventListener('x',()=>calls++);"
        "dispatchEvent(new Event('x'));if(calls!==1)throw Error('retired listener ran');",
        "event-retirement.js"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_event_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    const char *code[] = {
        "try{addEventListener({toString(){retireEvents();return 'x';}},()=>{});throw Error('accepted');}"
        "catch(e){if(e.name!=='InvalidStateError')throw e;}",
        "try{addEventListener('x',()=>{},{get capture(){retireEvents();return true;}});throw Error('accepted');}"
        "catch(e){if(e.name!=='InvalidStateError')throw e;}"};
    ck_assert(exec_script(dom_thread, code[_i], "event-conversion-retirement.js"));
}
END_TEST

/* DOM dispatch/get-parent rules; assertions stay outside callbacks so the
 * browser's required listener-exception isolation cannot hide test failures. */
START_TEST(test_quickjs_node_event_path)
{
    ck_assert(exec_script(dom_thread,
        "var t=document.getElementById('target'), chain=[t,document.body,document.documentElement,document,window];"
        "if(!(t instanceof EventTarget)||!(document instanceof EventTarget)||Object.getPrototypeOf(Node)!==EventTarget||"
        "Object.getPrototypeOf(Node.prototype)!==EventTarget.prototype)throw Error('inheritance');"
        "var ev=new CustomEvent('tree',{bubbles:true,cancelable:true,detail:t}), calls=[];"
        "chain.forEach((node,i)=>[true,false].forEach(capture=>node.addEventListener('tree',function(e){"
        "if(this!==node||e!==ev||e.target!==t||e.currentTarget!==node||e.detail!==t||e.isTrusted)throw Error('identity');"
        "var p=e.composedPath();if(p.length!==chain.length||p.some((n,j)=>n!==chain[j]))throw Error('path');"
        "p.length=0;calls.push(i+':'+e.eventPhase+':'+capture);if(i===3&&!capture)e.preventDefault();},capture)));"
        "if(t.dispatchEvent(ev)||calls.join()!=='4:1:true,3:1:true,2:1:true,1:1:true,0:2:true,0:2:false,1:3:false,2:3:false,3:3:false,4:3:false')"
        "throw Error('propagation '+calls);"
        "if(ev.target!==t||ev.currentTarget!==null||ev.eventPhase!==0||ev.composedPath().length)throw Error('cleanup');"
        "ev.initCustomEvent('tree',false,false,t);calls=[];t.dispatchEvent(ev);"
        "if(calls.join()!=='4:1:true,3:1:true,2:1:true,1:1:true,0:2:true,0:2:false')throw Error('non-bubbling '+calls);",
        "node-event-path.js"));
}
END_TEST

START_TEST(test_quickjs_node_event_frozen_path)
{
    ck_assert(exec_script(dom_thread,
        "var t=document.getElementById('target'), old=t.parentNode, chain=[t,old,document.documentElement,document,window];"
        "var seen=[], phase='first';"
        "function collect(e){var p=e.composedPath();if(p.length!==chain.length||p.some((n,i)=>n!==chain[i]))throw Error('moved path');"
        "seen.push(phase+':'+(this===window?'window':this===document?'document':this===old?'old':'target'));}"
        "window.addEventListener('move',function(e){old.removeChild(t);document.head.appendChild(t);"
        "old.addEventListener('move',collect);document.addEventListener('move',collect);collect.call(this,e);},{capture:true,once:true});"
        "t.addEventListener('move',collect);t.dispatchEvent(new Event('move',{bubbles:true}));"
        "if(seen.join()!=='first:window,first:target,first:old,first:document')throw Error('frozen '+seen);"
        "phase='second';chain=[t,document.head,document.documentElement,document,window];"
        "t.dispatchEvent(new Event('move',{bubbles:true}));"
        "if(seen.join()!=='first:window,first:target,first:old,first:document,second:target,second:document')throw Error('new path '+seen);",
        "node-event-frozen-path.js"));
}
END_TEST

START_TEST(test_quickjs_node_event_roots)
{
    ck_assert(exec_script(dom_thread,
        "var seen=[], t=document.getElementById('target');"
        "window.addEventListener('load',()=>seen.push('window'),true);"
        "document.addEventListener('load',e=>seen.push(e.composedPath().length),true);"
        "t.dispatchEvent(new Event('load',{bubbles:true}));document.dispatchEvent(new Event('load',{bubbles:true}));"
        "if(seen.join()!=='4,1')throw Error('load parent '+seen);"
        "var fragment=document.createDocumentFragment(), text=document.createTextNode('x');fragment.appendChild(text);"
        "var got=0;window.addEventListener('detached',()=>got+=100,true);document.addEventListener('detached',()=>got+=100,true);"
        "fragment.addEventListener('detached',function(e){var p=e.composedPath();"
        "if(this!==fragment||e.target!==text||p.length!==2||p[0]!==text||p[1]!==fragment)throw Error('fragment');got++;});"
        "text.dispatchEvent(new Event('detached',{bubbles:true}));"
        "var el=document.createElement('div');el.addEventListener('detached',e=>{if(e.composedPath().length!==1)throw Error('root');got++;});"
        "el.dispatchEvent(new Event('detached',{bubbles:true}));if(got!==2)throw Error('detached leaked '+got);"
        "var docPath=false;window.addEventListener('doc',e=>{docPath=e.target===document&&e.currentTarget===window&&"
        "e.composedPath().length===2;},true);document.dispatchEvent(new Event('doc'));if(!docPath)throw Error('document parent');",
        "node-event-roots.js"));
}
END_TEST

START_TEST(test_quickjs_node_event_passive)
{
    ck_assert(exec_script(dom_thread,
        "var roots=[window,document,document.documentElement,document.body];"
        "var other=[document.getElementById('target'),document.createElement('body'),document.getElementById('svg')];"
        "function cancel(e){e.preventDefault();}"
        "for(let type of ['wheel','mousewheel','touchstart','touchmove'])for(let node of roots.concat(other)){"
        "node.addEventListener(type,cancel);let result=node.dispatchEvent(new Event(type,{cancelable:true}));"
        "node.removeEventListener(type,cancel);if(result!==roots.includes(node))throw Error('default passive');"
        "node.addEventListener(type,cancel,{passive:false});result=node.dispatchEvent(new Event(type,{cancelable:true}));"
        "node.removeEventListener(type,cancel);if(result)throw Error('explicit active');}"
        "var old=document.body, body=document.createElement('body');document.documentElement.replaceChild(body,old);"
        "old.addEventListener('wheel',cancel);body.addEventListener('wheel',cancel);"
        "if(old.dispatchEvent(new Event('wheel',{cancelable:true}))||!body.dispatchEvent(new Event('wheel',{cancelable:true})))"
        "throw Error('body identity');",
        "node-event-passive.js"));
}
END_TEST

START_TEST(test_quickjs_node_event_nested)
{
    ck_assert(exec_script(dom_thread,
        "var t=document.getElementById('target'), order=[], outer=new Event('outer',{bubbles:true}), inner=new Event('inner');"
        "document.addEventListener('inner',function(e){if(e!==inner||e.target!==document||e.currentTarget!==document)throw Error('inner');"
        "if(outer.currentTarget!==t||outer.composedPath().length!==5)throw Error('outer lost');order.push('inner');});"
        "t.addEventListener('outer',function(e){order.push('target');document.dispatchEvent(inner);"
        "if(e.currentTarget!==t||e.composedPath().length!==5||inner.currentTarget!==null)throw Error('restore');order.push('restored');},{once:true});"
        "document.addEventListener('outer',function(e){if(e.currentTarget!==document)throw Error('bubble');order.push('document');});"
        "t.dispatchEvent(outer);t.dispatchEvent(outer);"
        "if(order.join()!=='target,inner,restored,document,document')throw Error('nested '+order);",
        "node-event-nested.js"));
}
END_TEST

START_TEST(test_quickjs_node_event_native_guard)
{
    ck_assert(exec_script(dom_thread,
        "var target=document.getElementById('target'), hits=0;"
        "document.addEventListener('DOMSubtreeModified',()=>hits++,true);target.textContent='changed';"
        "if(hits)throw Error('exposed obsolete mutation events');"
        "target.dispatchEvent(new Event('DOMSubtreeModified',{bubbles:true}));if(hits!==1)throw Error('explicit custom type');",
        "node-event-native-guard.js"));
}
END_TEST

START_TEST(test_quickjs_node_event_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var t=document.getElementById('target'), calls=[], e=new Event('retire',{bubbles:true});"
        "window.addEventListener('retire',function(e){retireEvents();"
        "if(e.currentTarget!==window||e.composedPath()[0]!==t)throw Error('retired identity');calls.push('window');},true);"
        "t.addEventListener('retire',()=>calls.push('target'));t.dispatchEvent(e);"
        "if(calls.join()!=='window'||e.currentTarget!==null||e.composedPath().length)throw Error('retirement');",
        "node-event-retirement.js"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_node_event_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "try{document.body.addEventListener('x',()=>{}, {get capture(){retireEvents();return false;}});throw Error('accepted');}"
        "catch(e){if(e.name!=='InvalidStateError')throw e;}",
        "node-event-conversion-retirement.js"));
}
END_TEST

START_TEST(test_quickjs_node_event_long_path)
{
    ck_assert(exec_script(dom_thread,
        "var root=document.createElement('div'), t=root;for(let i=0;i<80;i++){let n=document.createElement('i');t.appendChild(n);t=n;}"
        "document.body.appendChild(root);var seen=0;window.addEventListener('deep',e=>{"
        "if(e.target!==t||e.composedPath().length!==85)throw Error('deep path');seen++;},true);"
        "t.dispatchEvent(new Event('deep'));if(seen!==1)throw Error('deep delivery');",
        "node-event-long-path.js"));
}
END_TEST

START_TEST(test_quickjs_ui_event_values)
{
    ck_assert(exec_script(dom_thread,
        "var e=new UIEvent('resize');"
        "if(!(e instanceof UIEvent)||!(e instanceof Event)||e instanceof CustomEvent||e.view!==null||e.detail!==0||"
        "e.isTrusted||e.bubbles||e.cancelable||e.composed||e.target!==null)throw Error('UI defaults');"
        "if(Object.getPrototypeOf(UIEvent)!==Event||Object.getPrototypeOf(UIEvent.prototype)!==Event.prototype||"
        "Object.prototype.toString.call(e)!=='[object UIEvent]')throw Error('UI interface');"
        "var u=new UIEvent('x',{view:window,detail:4294967295.8,bubbles:true,cancelable:true,composed:true});"
        "if(u.view!==window||u.detail!==-1||!u.bubbles||!u.cancelable||!u.composed)throw Error('UI values');"
        "class Mine extends UIEvent{};if(!(new Mine('x') instanceof Mine))throw Error('UI subclass');"
        "for(let f of [()=>UIEvent('x'),()=>new UIEvent(),()=>new UIEvent('x',1),()=>new UIEvent('x',{view:{}}),"
        "()=>new UIEvent('x',{view:document}),()=>Object.getOwnPropertyDescriptor(UIEvent.prototype,'detail').get.call(new CustomEvent('x'))])"
        "{try{f();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}",
        "ui-event-values.js"));
}
END_TEST

START_TEST(test_quickjs_mouse_event_values)
{
    ck_assert(exec_script(dom_thread,
        "var e=new MouseEvent('move');"
        "if(!(e instanceof MouseEvent)||!(e instanceof UIEvent)||!(e instanceof Event)||e instanceof CustomEvent||"
        "e.view!==null||e.detail!==0||e.relatedTarget!==null||e.clientX!==0||e.clientY!==0||e.screenX!==0||e.screenY!==0||"
        "e.button!==0||e.buttons!==0||e.ctrlKey||e.altKey||e.shiftKey||e.metaKey||e.isTrusted)throw Error('mouse defaults');"
        "if(Object.getPrototypeOf(MouseEvent)!==UIEvent||Object.getPrototypeOf(MouseEvent.prototype)!==UIEvent.prototype||"
        "Object.prototype.toString.call(e)!=='[object MouseEvent]'||MouseEvent.AT_TARGET!==2)throw Error('mouse interface');"
        "var m=new MouseEvent('x',{view:window,relatedTarget:document.body,clientX:12.25,clientY:-3.75,screenX:1e20,screenY:-0,"
        "detail:2147483648,button:65535.9,buttons:-1,ctrlKey:true,shiftKey:1});"
        "if(m.view!==window||m.relatedTarget!==document.body||m.clientX!==12.25||m.x!==m.clientX||m.clientY!==-3.75||m.y!==m.clientY||"
        "m.screenX!==1e20||!Object.is(m.screenY,-0)||m.detail!==-2147483648||m.button!==-1||m.buttons!==65535||!m.ctrlKey||!m.shiftKey)"
        "throw Error('mouse values');"
        "class Mine extends MouseEvent{};if(!(new Mine('x') instanceof Mine))throw Error('mouse subclass');"
        "for(let target of [window,document,document.body,document.createTextNode(''),document.createDocumentFragment(),"
        "new EventTarget(),new AbortController().signal])if(new MouseEvent('x',{relatedTarget:target}).relatedTarget!==target)throw Error('related identity');"
        "for(let f of [()=>MouseEvent('x'),()=>new MouseEvent(),()=>new MouseEvent('x',{relatedTarget:{}}),"
        "()=>new MouseEvent('x',{relatedTarget:new Proxy(document.body,{})}),"
        "()=>Object.getOwnPropertyDescriptor(MouseEvent.prototype,'clientX').get.call(new UIEvent('x'))])"
        "{try{f();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}"
        "for(let key of ['clientX','clientY','screenX','screenY'])for(let value of [NaN,Infinity,-Infinity,Symbol(),1n]){"
        "try{new MouseEvent('x',{[key]:value});throw Error('accepted coordinate');}catch(e){if(!(e instanceof TypeError))throw e;}}",
        "mouse-event-values.js"));
}
END_TEST

START_TEST(test_quickjs_input_event_conversion)
{
    ck_assert(exec_script(dom_thread,
        "var order=[], dict=new Proxy({}, {get(o,key){order.push(key);return undefined;}});"
        "var type={toString(){order.push('type');return 'x';}}, ctor=new Proxy(function(){},{get(o,key){"
        "if(key==='prototype'){order.push(key);return MouseEvent.prototype;}return o[key];}});"
        "Reflect.construct(MouseEvent,[type,dict],ctor);"
        "if(order.join()!=='type,bubbles,cancelable,composed,detail,view,altKey,ctrlKey,metaKey,modifierAltGraph,modifierCapsLock,"
        "modifierFn,modifierFnLock,modifierHyper,modifierNumLock,modifierScrollLock,modifierSuper,modifierSymbol,modifierSymbolLock,"
        "shiftKey,button,buttons,clientX,clientY,relatedTarget,screenX,screenY,prototype')throw Error('conversion order '+order);"
        "var marker={}, later=false;try{new MouseEvent('x',{get clientX(){throw marker;},get clientY(){later=true;}});throw Error('accepted');}"
        "catch(e){if(e!==marker||later)throw Error('conversion exception');}"
        "var reentrant=new MouseEvent('x',{clientX:{valueOf(){new MouseEvent('nested');return 4.5;}}});"
        "if(reentrant.clientX!==4.5)throw Error('reentrant construction');"
        "Object.defineProperty(Event,Symbol.hasInstance,{value(){throw Error('instance trap');},configurable:true});"
        "var e=new MouseEvent('x');Event.prototype.preventDefault.call(e);delete Event[Symbol.hasInstance];",
        "input-event-conversion.js"));
}
END_TEST

START_TEST(test_quickjs_mouse_event_modifiers)
{
    ck_assert(exec_script(dom_thread,
        "var fields=['altKey','ctrlKey','metaKey','modifierAltGraph','modifierCapsLock','modifierFn','modifierFnLock','modifierHyper',"
        "'modifierNumLock','modifierScrollLock','modifierSuper','modifierSymbol','modifierSymbolLock','shiftKey'];"
        "var keys=['Alt','Control','Meta','AltGraph','CapsLock','Fn','FnLock','Hyper','NumLock','ScrollLock','Super','Symbol','SymbolLock','Shift'];"
        "for(let i=0;i<fields.length;i++){let e=new MouseEvent('x',{[fields[i]]:true});"
        "for(let j=0;j<keys.length;j++)if(e.getModifierState(keys[j])!==(i===j))throw Error(fields[i]+' / '+keys[j]);}"
        "var e=new MouseEvent('x',{ctrlKey:true});if(e.getModifierState('control')||e.getModifierState('Control\\0x')||e.getModifierState('unknown'))"
        "throw Error('unknown modifier');"
        "if(!e.getModifierState({toString(){return 'Control';}}))throw Error('key conversion');"
        "for(let f of [()=>e.getModifierState(),()=>e.getModifierState(Symbol()),()=>e.getModifierState.call(new Event('x'),'Control')])"
        "{try{f();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}",
        "mouse-event-modifiers.js"));
}
END_TEST

START_TEST(test_quickjs_input_event_initializers)
{
    ck_assert(exec_script(dom_thread,
        "var t=new EventTarget(), e=new MouseEvent('old',{clientX:3.5,view:window,relatedTarget:t,cancelable:true,composed:true});"
        "t.dispatchEvent(e);e.preventDefault();e.initUIEvent('ui',true,true,null,4.9);"
        "if(e.type!=='ui'||e.target!==null||e.view!==null||e.detail!==4||e.defaultPrevented||e.clientX!==3.5||e.relatedTarget!==t)throw Error('UI init');"
        "e.initMouseEvent('mouse',false,true,window,2,12.8,-13.5,14.25,-15.75,true,false,true,false,65535,t);"
        "if(e.type!=='mouse'||e.view!==window||e.detail!==2||e.screenX!==12||e.screenY!==-13||e.clientX!==14||e.clientY!==-15||"
        "!e.ctrlKey||e.altKey||!e.shiftKey||e.metaKey||e.button!==-1||e.relatedTarget!==t||e.isTrusted)throw Error('mouse init');"
        "var before=e.type, marker={};try{e.initMouseEvent('bad',false,false,null,{valueOf(){throw marker;}});}"
        "catch(x){if(x!==marker)throw x;}if(e.type!==before)throw Error('partial initialization');"
        "var converted=false, ran=false;t.addEventListener('mouse',()=>{"
        "e.initMouseEvent({toString(){converted=true;return 'ignored';}},true,false,null,99);"
        "if(e.type!=='mouse'||e.detail!==2||e.view!==window)throw Error('active init');ran=true;});"
        "t.dispatchEvent(e);if(!converted||!ran||e.type!=='mouse')throw Error('active conversion');"
        "e.initMouseEvent('reset');if(e.clientX||e.ctrlKey||e.shiftKey||e.button||e.relatedTarget!==null||e.view!==null||e.detail)throw Error('init defaults');"
        "for(let f of [()=>e.initMouseEvent(),()=>e.initUIEvent(),()=>e.initMouseEvent('x',false,false,{}),"
        "()=>MouseEvent.prototype.initMouseEvent.call(new UIEvent('x'),'x')])"
        "{try{f();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}",
        "input-event-initializers.js"));
}
END_TEST

START_TEST(test_quickjs_input_event_dispatch)
{
    ck_assert(exec_script(dom_thread,
        "var t=document.getElementById('target'), e=new MouseEvent('probe',{bubbles:true,cancelable:true,view:window,relatedTarget:t,"
        "clientX:12.25,buttons:3,ctrlKey:true}), calls=[];"
        "window.addEventListener('probe',ev=>{if(ev!==e||ev.view!==window||ev.relatedTarget!==t||ev.clientX!==12.25)throw Error('input data');"
        "calls.push('capture');},true);"
        "t.addEventListener('probe',function(ev){if(this!==t||!(ev instanceof MouseEvent)||ev.target!==t)throw Error('receiver');"
        "ev.preventDefault();calls.push('target');});"
        "document.addEventListener('probe',ev=>{if(ev.currentTarget!==document||!ev.ctrlKey||ev.buttons!==3)throw Error('bubbled input');"
        "calls.push('document');});"
        "if(t.dispatchEvent(e)||calls.join()!=='capture,target,document'||e.currentTarget!==null||e.composedPath().length)throw Error('dispatch '+calls);"
        "if(e.relatedTarget!==t||e.view!==window||e.clientX!==12.25)throw Error('saved input');",
        "input-event-dispatch.js"));
}
END_TEST

START_TEST(test_quickjs_input_event_gc)
{
    ck_assert(exec_script(dom_thread,
        "function inputCycles(){for(let i=0;i<100;i++){let t=new EventTarget(), e=new MouseEvent('x',{relatedTarget:t,view:window});"
        "t.addEventListener('x',()=>e);t.dispatchEvent(e);}}inputCycles();", "input event cycles"));
    JS_RunGC(dom_heap->rt);
    JSMemoryUsage before, after;
    JS_ComputeMemoryUsage(dom_heap->rt, &before);
    ck_assert(exec_script(dom_thread, "inputCycles();", "more input event cycles"));
    JS_RunGC(dom_heap->rt);
    JS_ComputeMemoryUsage(dom_heap->rt, &after);
    ck_assert_msg(after.obj_count <= before.obj_count + 2, "Input event cycles leaked %lld objects",
        (long long)(after.obj_count - before.obj_count));
    ck_assert(exec_script(dom_thread,
        "var saved, hits=0;{let t=new EventTarget();t.addEventListener('x',()=>hits++);saved=new MouseEvent('x',{relatedTarget:t});}",
        "saved related target"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "(saved.relatedTarget.dispatchEvent(new Event('x')),hits===1)", "related target kept alive");
    ck_assert(exec_script(dom_thread, "saved=null;", "release related target"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_input_event_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var t=new EventTarget(), related=document.body, e=new MouseEvent('x',{view:window,relatedTarget:related}), called=false;"
        "t.addEventListener('x',()=>{retireEvents();if(e.view!==window||e.relatedTarget!==related)throw Error('retired input');called=true;});"
        "t.dispatchEvent(e);if(!called||e.currentTarget!==null||e.composedPath().length)throw Error('retired dispatch');",
        "input-event-retirement.js"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_input_event_conversion_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "try{new MouseEvent('x',{get clientX(){retireEvents();return 3;}});throw Error('accepted');}"
        "catch(e){if(e.name!=='InvalidStateError')throw e;}", "input-conversion-retirement.js"));
}
END_TEST

static bool native_input(const char *type, const struct js_mouse_event *mouse)
{
    dom_string *name, *id;
    dom_node *target;
    ck_assert_int_eq(dom_string_create((const uint8_t *)type, strlen(type), &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_string_create((const uint8_t *)"target", 6, &id), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(dom_html.document, id, &target), DOM_NO_ERR);
    ck_assert_ptr_nonnull(target);
    bool accepted = js_fire_input_event(dom_thread, name, target, mouse);
    dom_node_unref(target);
    dom_string_unref(id);
    dom_string_unref(name);
    return accepted;
}

static bool native_submit(bool with_button)
{
    dom_string *id;
    dom_node *form = NULL, *button = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"native-form", 11, &id), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(dom_html.document, id, &form), DOM_NO_ERR);
    dom_string_unref(id);
    ck_assert_ptr_nonnull(form);
    if (with_button) {
        ck_assert_int_eq(dom_string_create((const uint8_t *)"native-button", 13, &id), DOM_NO_ERR);
        ck_assert_int_eq(dom_document_get_element_by_id(dom_html.document, id, &button), DOM_NO_ERR);
        dom_string_unref(id);
        ck_assert_ptr_nonnull(button);
    }
    bool accepted = js_fire_submit_event(dom_thread, form, button);
    dom_node_unref(button);
    dom_node_unref(form);
    return accepted;
}

static void native_form_setup(void)
{
    ck_assert(exec_script(dom_thread,
        "var form=document.createElement('form'),button=document.createElement('button'),saved,seen=[],NativeSubmit=SubmitEvent;"
        "form.id='native-form';button.id='native-button';form.appendChild(button);document.body.appendChild(form);",
        "native-form.js"));
}

START_TEST(test_quickjs_document_readiness)
{
    assert_predicate(dom_thread, "document.readyState==='loading'", "initial readiness");
    dom_html.readiness = HTML_DOCUMENT_INTERACTIVE;
    assert_predicate(dom_thread, "document.readyState==='interactive'", "parser readiness");
    dom_html.readiness = HTML_DOCUMENT_COMPLETE;
    assert_predicate(dom_thread,
        "(()=>{const d=Object.getOwnPropertyDescriptor(Document.prototype,'readyState');"
        "let rejected=false;try{d.get.call({});}catch(e){rejected=e instanceof TypeError;}"
        "document.readyState='loading';return d.enumerable&&d.configurable&&!d.set&&rejected&&"
        "document.readyState==='complete';})()",
        "readonly branded readiness accessor");
}
END_TEST

START_TEST(test_quickjs_document_lifecycle)
{
    ck_assert(exec_script(dom_thread,
        "var calls=[],events=[],valid=true,OriginalEvent=Event;"
        "function listen(target,type,label,capture=false){target.addEventListener(type,function(e){"
        "calls.push(label);events.push(e);valid=valid&&e instanceof OriginalEvent&&e.isTrusted&&"
        "!e.cancelable&&!e.composed&&e.target===document&&e.currentTarget===target&&this===target;"
        "e.preventDefault();valid=valid&&!e.defaultPrevented;"
        "if(type==='DOMContentLoaded')valid=valid&&e.bubbles&&e.composedPath()[0]===document&&"
        "e.composedPath()[1]===window;"
        "if(type==='load')valid=valid&&!e.bubbles&&e.eventPhase===Event.AT_TARGET&&"
        "e.composedPath().length===1&&e.composedPath()[0]===window;"
        "queueMicrotask(()=>calls.push(label+'-job'));},capture);}"
        "listen(window,'readystatechange','capture',true);listen(document,'readystatechange','state');"
        "listen(window,'readystatechange','should-not-bubble');"
        "listen(document,'DOMContentLoaded','dom');listen(window,'DOMContentLoaded','window-dom');"
        "listen(window,'load','load');listen(document,'load','should-not-load',true);",
        "lifecycle-listeners.js"));
    ck_assert(js_fire_document_event(dom_thread, JS_DOCUMENT_READYSTATE));
    ck_assert(js_fire_document_event(dom_thread, JS_DOCUMENT_DOM_CONTENT_LOADED));
    ck_assert(js_fire_document_event(dom_thread, JS_DOCUMENT_LOAD));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread,
        "valid&&calls.join(',')==='capture,capture-job,state,state-job,dom,dom-job,window-dom,window-dom-job,load,load-job'&&"
        "events.every(e=>e.target===document&&e.currentTarget===null&&e.eventPhase===0&&e.composedPath().length===0)",
        "trusted document events, Window load override and listener checkpoints");
    ck_assert(exec_script(dom_thread, "window.Event=function(){throw new Error('page constructor called');};",
        "replace-event-constructor.js"));
    ck_assert(js_fire_document_event(dom_thread, JS_DOCUMENT_DOM_CONTENT_LOADED));
    assert_predicate(dom_thread, "valid&&calls.length===14", "host event creation ignores page constructor");
}
END_TEST

START_TEST(test_quickjs_document_lifecycle_allocation_failure)
{
    ck_assert(
        exec_script(dom_thread, "var calls=0;window.addEventListener('load',()=>calls++);", "lifecycle-allocation.js"));
    bool failed = false, succeeded = false;
    for (size_t extra = 0; extra <= 32768; extra += 128) {
        JS_RunGC(dom_heap->rt);
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(dom_heap->rt, &usage);
        JS_SetMemoryLimit(dom_heap->rt, usage.malloc_size + extra);
        bool result = js_fire_document_event(dom_thread, JS_DOCUMENT_LOAD);
        JS_SetMemoryLimit(dom_heap->rt, 0);
        ck_assert_uint_eq(dom_heap->execution_depth, 0);
        ck_assert_uint_eq(dom_heap->native_dispatch_depth, 0);
        failed |= !result;
        if (result) {
            succeeded = true;
            break;
        }
    }
    ck_assert(failed && succeeded);
    ck_assert(js_fire_document_event(dom_thread, JS_DOCUMENT_LOAD));
    assert_predicate(dom_thread, "calls>=2", "lifecycle allocation recovery");
}
END_TEST

START_TEST(test_quickjs_native_submit)
{
    native_form_setup();
    ck_assert(exec_script(dom_thread,
        "window.addEventListener('submit',e=>seen.push('capture'),true);"
        "form.addEventListener('submit',e=>{saved=e;seen.push('target');"
        "if(!(e instanceof NativeSubmit)||!e.isTrusted||e.composed||!e.bubbles||!e.cancelable||"
        "e.target!==form||e.currentTarget!==form||e.submitter!==button)throw Error('submit payload');"
        "button.parentNode.removeChild(button);queueMicrotask(()=>{seen.push('microtask');e.preventDefault();});});"
        "document.addEventListener('submit',e=>{if(!e.defaultPrevented)throw Error('checkpoint');seen.push('bubble');});"
        /* Native factories must not call page constructors. */
        "SubmitEvent=function(){throw Error('page constructor');};",
        "native-submit.js"));
    ck_assert(!native_submit(true));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread,
        "seen.join()==='capture,target,microtask,bubble'&&saved.submitter===button&&!button.isConnected&&"
        "saved.target===form&&saved.currentTarget===null&&saved.composedPath().length===0",
        "retained native submitter and path cleanup");
}
END_TEST

START_TEST(test_quickjs_native_submit_without_button)
{
    native_form_setup();
    ck_assert(exec_script(dom_thread,
        "form.addEventListener('submit',e=>{saved=e;seen.push(e.submitter===null&&e.isTrusted);});",
        "implicit-submit.js"));
    ck_assert(native_submit(false));
    assert_predicate(dom_thread, "seen.length===1&&seen[0]&&!saved.defaultPrevented", "implicit null submitter");
}
END_TEST

START_TEST(test_quickjs_native_submit_allocation_failure)
{
    native_form_setup();
    ck_assert(exec_script(dom_thread,
        "var calls=0,valid=true;form.addEventListener('submit',e=>{calls++;valid=valid&&e.submitter===button;});",
        "submit-allocation.js"));
    bool failed = false, succeeded = false;
    for (size_t extra = 0; extra <= 32768; extra += 128) {
        JS_RunGC(dom_heap->rt);
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(dom_heap->rt, &usage);
        JS_SetMemoryLimit(dom_heap->rt, usage.malloc_size + extra);
        bool result = native_submit(true);
        JS_SetMemoryLimit(dom_heap->rt, 0);
        ck_assert_uint_eq(dom_heap->execution_depth, 0);
        ck_assert_uint_eq(dom_heap->native_dispatch_depth, 0);
        failed |= !result;
        if (result) {
            succeeded = true;
            break;
        }
    }
    ck_assert(failed && succeeded);
    ck_assert(native_submit(true));
    assert_predicate(dom_thread, "calls>=2&&valid", "submit allocation recovery");
}
END_TEST

START_TEST(test_quickjs_native_click)
{
    ck_assert(exec_script(dom_thread,
        "var target=document.getElementById('target'),seen=[],saved;"
        "window.addEventListener('click',e=>{seen.push('window');},true);"
        "document.addEventListener('click',e=>{seen.push('document');},true);"
        "target.addEventListener('click',e=>{saved=e;seen.push('target');"
        "if(!(e instanceof PointerEvent)||!(e instanceof MouseEvent)||!e.isTrusted||!e.composed||"
        "!e.bubbles||!e.cancelable||e.target!==target||e.currentTarget!==target||e.view!==window||"
        "e.pointerId!==1||e.pointerType!=='mouse'||!e.isPrimary||e.button!==0||e.buttons!==0||"
        "e.pressure!==0||e.width!==1||e.height!==1||e.detail!==2||e.clientX!==12.25||e.clientY!==34.5||"
        "e.screenX!==56.75||e.screenY!==78.5||!e.shiftKey||!e.ctrlKey||e.altKey||e.metaKey)throw Error('payload');"
        "queueMicrotask(()=>{seen.push('microtask');e.preventDefault();});});"
        "document.addEventListener('click',e=>{seen.push('bubble');if(!e.defaultPrevented)throw Error('checkpoint');});",
        "native-click.js"));
    struct js_mouse_event mouse = {.client_x = 12.25,
        .client_y = 34.5,
        .screen_x = 56.75,
        .screen_y = 78.5,
        .detail = 2,
        .shift = true,
        .ctrl = true};
    ck_assert(!native_input("click", &mouse));
    assert_predicate(dom_thread,
        "seen.join()==='window,document,target,microtask,bubble'&&saved.currentTarget===null&&"
        "saved.composedPath().length===0&&saved.target===target&&saved.defaultPrevented",
        "native path and cleanup");
}
END_TEST

START_TEST(test_quickjs_native_click_defaults)
{
    ck_assert(exec_script(dom_thread,
        "var seen=[];document.getElementById('target').addEventListener('click',e=>{"
        "seen.push(e instanceof PointerEvent&&e.pointerId===-1&&e.pointerType===''&&!e.isPrimary&&"
        "e.detail===0&&e.clientX===0&&e.screenY===0&&e.isTrusted);});"
        "for(let type of ['input','change'])document.addEventListener(type,e=>{"
        "seen.push(e.constructor===Event&&e.isTrusted&&!e.cancelable&&e.bubbles&&e.composed===(type==='input'));"
        "e.preventDefault();});",
        "native-defaults.js"));
    ck_assert(native_input("click", NULL));
    ck_assert(native_input("input", NULL));
    ck_assert(native_input("change", NULL));
    assert_predicate(dom_thread, "seen.length===3&&seen.every(Boolean)", "native interface defaults");
}
END_TEST

START_TEST(test_quickjs_native_click_mutation)
{
    ck_assert(exec_script(dom_thread,
        "var target=document.getElementById('target'), seen=[];"
        "target.addEventListener('click',e=>{target.parentNode.removeChild(target);"
        "target.id='detached';seen.push('removed');target.dispatchEvent(new Event('nested'));});"
        "target.addEventListener('nested',()=>seen.push('nested'));"
        "document.addEventListener('click',e=>{seen.push('bubble');if(e.target!==target)throw Error('identity');});",
        "native-mutation.js"));
    ck_assert(native_input("click", NULL));
    assert_predicate(dom_thread, "seen.join()==='removed,nested,bubble'&&!target.isConnected", "native frozen path");
}
END_TEST

START_TEST(test_quickjs_native_click_exception)
{
    ck_assert(exec_script(dom_thread,
        "var seen=0,t=document.getElementById('target');t.addEventListener('click',()=>{throw Error('ordinary');});"
        "t.addEventListener('click',()=>seen++);",
        "native-exception.js"));
    ck_assert(native_input("click", NULL));
    assert_predicate(dom_thread, "seen===1", "native exception continues propagation");
}
END_TEST

START_TEST(test_quickjs_native_click_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var saved;document.getElementById('target').addEventListener('click',e=>{saved=e;retireEvents();e.preventDefault();});"
        "document.addEventListener('click',()=>{throw Error('retired listener ran');});",
        "native-retire.js"));
    ck_assert(!native_input("click", NULL));
    ck_assert_uint_eq(dom_heap->execution_depth, 0);
    ck_assert_uint_eq(dom_heap->native_dispatch_depth, 0);
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_native_click_timeout)
{
    ck_assert(exec_script(dom_thread,
        "var t=document.getElementById('target');t.addEventListener('click',()=>{while(true){}},{once:true});",
        "native-timeout.js"));
    ck_assert(!native_input("click", NULL));
    ck_assert_uint_eq(dom_heap->execution_depth, 0);
    ck_assert_uint_eq(dom_heap->native_dispatch_depth, 0);
    ck_assert(native_input("click", NULL));
    assert_predicate(dom_thread, "1+1===2", "native timeout recovery");
}
END_TEST

START_TEST(test_quickjs_native_click_allocation_failure)
{
    ck_assert(
        exec_script(dom_thread, "var calls=0;document.addEventListener('click',()=>calls++);", "native-allocation.js"));
    bool failed = false, succeeded = false;
    for (size_t extra = 0; extra <= 32768; extra += 128) {
        JS_RunGC(dom_heap->rt);
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(dom_heap->rt, &usage);
        JS_SetMemoryLimit(dom_heap->rt, usage.malloc_size + extra);
        bool result = native_input("click", NULL);
        JS_SetMemoryLimit(dom_heap->rt, 0);
        ck_assert_uint_eq(dom_heap->execution_depth, 0);
        ck_assert_uint_eq(dom_heap->native_dispatch_depth, 0);
        failed |= !result;
        if (result) {
            succeeded = true;
            break;
        }
    }
    ck_assert(failed && succeeded);
    ck_assert(native_input("click", NULL));
    assert_predicate(dom_thread, "calls>=2", "native dispatch allocation recovery");
}
END_TEST

START_TEST(test_quickjs_native_click_shared_budget)
{
    ck_assert(exec_script(dom_thread,
        "var elapsed=0, done=false, t=document.getElementById('target');"
        "t.addEventListener('click',()=>{let start=Date.now();while(Date.now()-start<600){}},{once:true});"
        "t.addEventListener('click',()=>{let start=Date.now();while(Date.now()-start<600){};done=true;},{once:true});",
        "native-shared-budget.js"));
    ck_assert(!native_input("click", NULL));
    assert_predicate(dom_thread, "!done", "listeners share the native dispatch deadline");
    ck_assert(native_input("click", NULL));
}
END_TEST

START_TEST(test_quickjs_pointer_event)
{
    assert_predicate(dom_thread,
        "(()=>{const e=new PointerEvent('click',{pointerId:17,pointerType:'pen',isPrimary:true,width:3,height:4,"
        "pressure:.25,tangentialPressure:.5,tiltX:-10,tiltY:20,twist:40,clientX:1.5,ctrlKey:true});"
        "return e instanceof MouseEvent&&e instanceof UIEvent&&e instanceof Event&&!e.isTrusted&&"
        "e.pointerId===17&&e.pointerType==='pen'&&e.isPrimary&&e.width===3&&e.height===4&&e.pressure===.25&&"
        "e.tangentialPressure===.5&&e.tiltX===-10&&e.tiltY===20&&e.twist===40&&e.clientX===1.5&&e.ctrlKey;})()",
        "PointerEvent constructor and inheritance");
    assert_predicate(dom_thread,
        "(()=>{let e=new PointerEvent('x');return e.pointerId===0&&e.pointerType===''&&!e.isPrimary&&"
        "e.width===1&&e.height===1&&e.pressure===0;})()",
        "PointerEvent defaults");
}
END_TEST

START_TEST(test_quickjs_submit_event_values)
{
    ck_assert(exec_script(dom_thread,
        "var e=new SubmitEvent('submit');"
        "if(!(e instanceof SubmitEvent)||!(e instanceof Event)||e instanceof UIEvent||e instanceof CustomEvent||"
        "e.submitter!==null||e.isTrusted||e.bubbles||e.cancelable||e.composed||e.target!==null)throw Error('defaults');"
        "if(Object.getPrototypeOf(SubmitEvent)!==Event||Object.getPrototypeOf(SubmitEvent.prototype)!==Event.prototype||"
        "Object.prototype.toString.call(e)!=='[object SubmitEvent]'||SubmitEvent.AT_TARGET!==2)throw Error('interface');"
        "for(let dict of [undefined,null,{}, {submitter:undefined},{submitter:null}])"
        "if(new SubmitEvent('submit',dict).submitter!==null)throw Error('null default');"
        "for(let tag of ['button','input','div','form','unknown']){let node=document.createElement(tag);"
        "node.setAttribute('disabled','');let e=new SubmitEvent('ș\\0submit',"
        "{submitter:node,bubbles:true,cancelable:true,composed:true});"
        "if(e.submitter!==node||e.type!=='ș\\0submit'||!e.bubbles||!e.cancelable||!e.composed)throw Error('values');"
        "if(Reflect.set(e,'submitter',null)||e.submitter!==node)throw Error('readonly');}"
        "var d=Object.getOwnPropertyDescriptor(SubmitEvent.prototype,'submitter');"
        "if(!d.enumerable||!d.configurable||d.set||typeof d.get!=='function')throw Error('descriptor');"
        "class Mine extends SubmitEvent{};if(!(new Mine('submit') instanceof Mine))throw Error('subclass');"
        "function Alternate(){};Alternate.prototype=42;"
        "if(Object.getPrototypeOf(Reflect.construct(SubmitEvent,['submit'],Alternate))!==SubmitEvent.prototype)"
        "throw Error('default prototype');",
        "submit-event-values.js"));
}
END_TEST

START_TEST(test_quickjs_submit_event_brands)
{
    ck_assert(exec_script(dom_thread,
        "function typeError(f){try{f();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}"
        "for(let value of [window,document,document.createTextNode(''),document.createComment(''),"
        "document.createDocumentFragment(),document.getElementById('svg'),{},1,false,Symbol(),"
        "Object.create(HTMLElement.prototype),new Proxy(document.body,{})])"
        "typeError(()=>new SubmitEvent('submit',{submitter:value}));"
        "for(let f of [()=>SubmitEvent('submit'),()=>new SubmitEvent(),()=>new SubmitEvent(Symbol()),"
        "()=>new SubmitEvent('submit',1)])typeError(f);"
        "var get=Object.getOwnPropertyDescriptor(SubmitEvent.prototype,'submitter').get;"
        "for(let value of [{},Object.create(SubmitEvent.prototype),new Event('x'),new CustomEvent('x'),"
        "new UIEvent('x'),new MouseEvent('x'),new Proxy(new SubmitEvent('x'),{})])typeError(()=>get.call(value));"
        "var e=new SubmitEvent('submit');"
        "for(let [proto,key] of [[UIEvent.prototype,'detail'],[UIEvent.prototype,'view'],"
        "[MouseEvent.prototype,'clientX'],[CustomEvent.prototype,'detail']])"
        "typeError(()=>Object.getOwnPropertyDescriptor(proto,key).get.call(e));"
        "typeError(()=>MouseEvent.prototype.getModifierState.call(e,'Control'));"
        "typeError(()=>UIEvent.prototype.initUIEvent.call(e,'x'));"
        "typeError(()=>MouseEvent.prototype.initMouseEvent.call(e,'x'));"
        "var node=document.createElement('button');Object.setPrototypeOf(node,null);"
        "Object.defineProperty(HTMLElement,Symbol.hasInstance,{value(){throw Error('page brand check');}});"
        "if(new SubmitEvent('submit',{submitter:node}).submitter!==node)throw Error('native identity');"
        "Object.setPrototypeOf(e,MouseEvent.prototype);typeError(()=>e.clientX);"
        "if(get.call(e)!==null)throw Error('changed event prototype');",
        "submit-event-brands.js"));
}
END_TEST

START_TEST(test_quickjs_submit_event_conversion)
{
    ck_assert(exec_script(dom_thread,
        "var order=[], node=document.createElement('button');"
        "var dict=new Proxy({}, {get(o,key){order.push(key);return key==='submitter'?node:undefined;}});"
        "var type={toString(){order.push('type');return 'submit';}};"
        "var ctor=new Proxy(function(){},{get(o,key){if(key==='prototype'){order.push(key);return SubmitEvent.prototype;}}});"
        "var e=Reflect.construct(SubmitEvent,[type,dict],ctor);"
        "if(order.join()!=='type,bubbles,cancelable,composed,submitter,prototype'||e.submitter!==node)"
        "throw Error('conversion order '+order);"
        "var marker={}, called=false;ctor=new Proxy(function(){},{get(){called=true;throw marker;}});"
        "try{Reflect.construct(SubmitEvent,['submit',{get submitter(){throw marker;}}],ctor);throw Error('accepted');}"
        "catch(e){if(e!==marker||called)throw Error('getter exception');}"
        "try{Reflect.construct(SubmitEvent,['submit',{submitter:node}],ctor);throw Error('accepted');}"
        "catch(e){if(e!==marker||!called)throw Error('prototype exception');}"
        "var nested=new SubmitEvent('submit',{get submitter(){new SubmitEvent('nested',{submitter:node});return node;}});"
        "if(nested.submitter!==node)throw Error('reentrant construction');",
        "submit-event-conversion.js"));
}
END_TEST

START_TEST(test_quickjs_submit_event_dispatch)
{
    ck_assert(exec_script(dom_thread,
        "var form=document.createElement('form'), button=document.createElement('button'), seen=[];"
        "button.id='submitter';form.appendChild(button);document.body.appendChild(form);"
        "var e=new SubmitEvent('submit',{submitter:button,bubbles:true,cancelable:true});"
        "window.addEventListener('submit',function(ev){"
        "seen.push(ev===e&&ev.submitter===button&&ev.target===form&&ev.currentTarget===window);"
        "form.removeChild(button);let replacement=document.createElement('button');replacement.id='submitter';"
        "form.appendChild(replacement);document.body.removeChild(form);},{capture:true,once:true});"
        "form.addEventListener('submit',function(ev){"
        "seen.push(ev.submitter===button&&ev.target===form&&ev.currentTarget===form&&!ev.isTrusted);ev.preventDefault();"
        "let inner=new SubmitEvent('nested');form.dispatchEvent(inner);seen.push(inner.submitter===null);});"
        "document.addEventListener('submit',function(ev){"
        "seen.push(ev.submitter===button&&ev.currentTarget===document&&ev.composedPath()[0]===form);});"
        "if(form.dispatchEvent(e)||seen.length!==4||seen.some(x=>x!==true)||!e.defaultPrevented||e.submitter!==button||"
        "e.target!==form||e.currentTarget!==null||e.composedPath().length)throw Error('dispatch '+seen);"
        "e.initEvent('again',true,true);"
        "if(e.submitter!==button||e.target!==null||e.defaultPrevented||e.type!=='again')throw Error('base initializer');"
        "if(!button.dispatchEvent(e)||e.target!==button||e.submitter!==button)throw Error('redispatch');",
        "submit-event-dispatch.js"));
}
END_TEST

START_TEST(test_quickjs_submit_event_gc)
{
    /* The document caches wrappers. Retire each context to ensure these cycles
     * really become unreachable rather than being kept alive by that cache. */
    JSMemoryUsage before, after;
    for (unsigned i = 0; i < 4; i++) {
        jsthread *thread;
        ck_assert_int_eq(js_newthread(dom_heap, NULL, &dom_html, &thread), NSERROR_OK);
        ck_assert(exec_script(thread,
            "var button=document.createElement('button'), e=new SubmitEvent('submit',{submitter:button});"
            "button.saved=e;button.expando={owner:button,event:e};", "submit event reference cycle"));
        js_destroythread(thread);
        JS_RunGC(dom_heap->rt);
        JS_ComputeMemoryUsage(dom_heap->rt, &after);
        if (i == 0)
            before = after;
        else
            ck_assert_msg(after.obj_count <= before.obj_count + 2, "Submit event cycles leaked %lld objects",
                (long long)(after.obj_count - before.obj_count));
    }
    ck_assert(exec_script(dom_thread,
        "var saved=new SubmitEvent('submit',{submitter:document.createElement('button')});"
        "saved.submitter.marker={event:saved};", "retain original submitter"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "saved.submitter.marker.event===saved", "submitter survives GC");
}
END_TEST

START_TEST(test_quickjs_submit_event_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var t=document.createElement('form'), button=document.createElement('button'), seen=[];"
        "var e=new SubmitEvent('submit',{submitter:button,cancelable:true});"
        "t.addEventListener('submit',ev=>{retireEvents();seen.push(ev.submitter===button);ev.preventDefault();});"
        "t.addEventListener('submit',()=>seen.push(false));"
        "if(t.dispatchEvent(e)||seen.length!==1||seen[0]!==true||e.currentTarget!==null||e.composedPath().length)"
        "throw Error('retired dispatch');", "submit-event-retirement.js"));
    JS_RunGC(dom_heap->rt);
    /* Use C-held values: new execution is barred after retirement, but event
     * data can still be read by the callback already on the stack. */
    global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue event = JS_GetPropertyStr(dom_thread->ctx, global, "e");
    JSValue button = JS_GetPropertyStr(dom_thread->ctx, global, "button");
    JSValue submitter = JS_GetPropertyStr(dom_thread->ctx, event, "submitter");
    ck_assert(JS_IsStrictEqual(dom_thread->ctx, button, submitter));
    JS_FreeValue(dom_thread->ctx, submitter);
    JS_FreeValue(dom_thread->ctx, button);
    JS_FreeValue(dom_thread->ctx, event);
    JS_FreeValue(dom_thread->ctx, global);
}
END_TEST

/* Every constructor must check retirement after both IDL conversion and the
 * new-target prototype lookup, including base Event and CustomEvent. */
enum { EVENT_CONVERSION_STAGES = 3, EVENT_CONSTRUCTOR_KINDS = 6 };
START_TEST(test_quickjs_event_constructor_retirement)
{
    static const char *names[] = {"Event", "CustomEvent", "UIEvent", "MouseEvent", "SubmitEvent", "PointerEvent"};
    static const char *calls[] = {
        "new C({toString(){retireEvents();return 'x';}})",
        "new C('x',{get bubbles(){retireEvents();return true;}})",
        "Reflect.construct(C,['x'],new Proxy(function(){},{get(){retireEvents();return C.prototype;}}))"};
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    char script[512];
    unsigned kind = _i / EVENT_CONVERSION_STAGES, stage = _i % EVENT_CONVERSION_STAGES;
    snprintf(script, sizeof(script),
        "var C=%s;try{%s;throw Error('accepted');}catch(e){if(e.name!=='InvalidStateError')throw e;}",
        names[kind], calls[stage]);
    ck_assert_msg(exec_script(dom_thread, script, "event-constructor-retirement.js"),
        "%s constructor retirement failed at conversion stage %u", names[kind], stage);
}
END_TEST

START_TEST(test_quickjs_abort_values)
{
    ck_assert(exec_script(dom_thread,
        "var c=new AbortController(), s=c.signal;"
        "if(c.signal!==s||!(c instanceof AbortController)||!(s instanceof AbortSignal)||!(s instanceof EventTarget)||"
        "Object.getPrototypeOf(AbortSignal)!==EventTarget||Object.getPrototypeOf(AbortSignal.prototype)!==EventTarget.prototype||"
        "Object.prototype.toString.call(s)!=='[object AbortSignal]'||s.aborted||s.reason!==undefined||s.throwIfAborted()!==undefined)"
        "throw Error('initial state');"
        "class Controller extends AbortController{};if(!(new Controller() instanceof Controller))throw Error('subclass');"
        "for(let f of [()=>AbortController(),()=>new AbortSignal(),()=>AbortSignal(),()=>AbortController.prototype.abort.call({}),"
        "()=>Object.getOwnPropertyDescriptor(AbortSignal.prototype,'reason').get.call({}),"
        "()=>AbortSignal.prototype.throwIfAborted.call({})]){try{f();throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}"
        "var intrinsic=DOMException;globalThis.DOMException=function(){throw Error('replaced constructor');};c.abort();"
        "if(!s.aborted||!(s.reason instanceof intrinsic)||s.reason.name!=='AbortError'||s.reason.code!==20)throw Error('default reason');"
        "var reason=s.reason;c.abort('replacement');if(s.reason!==reason)throw Error('repeated abort');"
        "try{s.throwIfAborted();throw Error('not thrown');}catch(e){if(e!==reason)throw e;}"
        "globalThis.DOMException=intrinsic;"
        "for(let reason of [null,false,0,'stop',{toString(){throw Error('coerced');}}]){let a=new AbortController();a.abort(reason);"
        "if(a.signal.reason!==reason)throw Error('reason identity');try{a.signal.throwIfAborted();throw Error('not thrown');}"
        "catch(e){if(e!==reason)throw Error('wrong thrown value');}}",
        "abort-values.js"));
}
END_TEST

START_TEST(test_quickjs_abort_event)
{
    ck_assert(exec_script(dom_thread,
        "var c=new AbortController(), s=c.signal, seen=[], saved;"
        "s.addEventListener('abort',function(e){if(this!==s||e.target!==s||e.currentTarget!==s||e.eventPhase!==2||"
        "e.bubbles||e.cancelable||e.composed||!e.isTrusted||!s.aborted||s.reason!==42)throw Error('abort event');"
        "if(e.composedPath().length!==1||e.composedPath()[0]!==s)throw Error('abort path');"
        "saved=e;seen.push('abort');e.preventDefault();c.abort(99);queueMicrotask(()=>seen.push('job'));});"
        "c.abort(42);seen.push('after');"
        "if(seen.join()!=='abort,after'||!saved||saved.currentTarget!==null||saved.defaultPrevented||saved.composedPath().length)"
        "throw Error('abort dispatch '+seen);"
        "var fake=new AbortController();fake.signal.dispatchEvent(new Event('abort'));if(fake.signal.aborted)throw Error('fake abort');",
        "abort-event.js"));
    assert_predicate(dom_thread, "seen.join()==='abort,after,job'", "abort microtask checkpoint");
}
END_TEST

START_TEST(test_quickjs_abort_static)
{
    ck_assert(exec_script(dom_thread,
        "var reason={id:1}, s=AbortSignal.abort(reason), other=AbortSignal.abort();"
        "if(!s.aborted||s.reason!==reason||!other.aborted||other.reason.name!=='AbortError'||s===other)throw Error('static');"
        "if(AbortSignal.abort.call({},null).reason!==null)throw Error('static receiver');"
        "var t=new EventTarget(), hit=0;t.addEventListener('x',()=>hit++,{signal:s});t.dispatchEvent(new Event('x'));"
        "if(hit)throw Error('already aborted registration');"
        "var gets=[];t.addEventListener('x',null,{get capture(){gets.push('capture');},get once(){gets.push('once');},"
        "get passive(){gets.push('passive');},get signal(){gets.push('signal');return s;}});"
        "if(gets.join()!=='capture,once,passive,signal')throw Error('conversion order');"
        "for(let signal of [null,{},Object.create(AbortSignal.prototype),new EventTarget()]){"
        "try{t.addEventListener('x',null,{signal});throw Error('accepted');}catch(e){if(!(e instanceof TypeError))throw e;}}"
        "t.removeEventListener('x',()=>{},{get signal(){throw Error('remove reads signal');}});",
        "abort-static.js"));
}
END_TEST

START_TEST(test_quickjs_abort_listeners)
{
    ck_assert(exec_script(dom_thread,
        "var c=new AbortController(), count=0, targets=[new EventTarget(),window,document,document.body];"
        "for(let t of targets)t.addEventListener('owned',()=>count++,{signal:c.signal});"
        "c.signal.addEventListener('abort',e=>e.stopImmediatePropagation());"
        "c.signal.addEventListener('abort',()=>count+=100);"
        "for(let t of targets)t.dispatchEvent(new Event('owned'));if(count!==4)throw Error('initial listeners');"
        "c.abort();for(let t of targets)t.dispatchEvent(new Event('owned'));if(count!==4)throw Error('cancellation');"
        "var d=new AbortController(), t=new EventTarget(), n=0;function f(){n++;}"
        "t.addEventListener('dup',f,{signal:d.signal});t.addEventListener('dup',f,{signal:c.signal});"
        "t.dispatchEvent(new Event('dup'));if(n!==1)throw Error('aborted duplicate');"
        "var second=new AbortController();t.addEventListener('dup',f,{signal:second.signal});second.abort();"
        "t.dispatchEvent(new Event('dup'));if(n!==2)throw Error('duplicate changed owner');d.abort();"
        "t.dispatchEvent(new Event('dup'));if(n!==2)throw Error('original owner lost');",
        "abort-listeners.js"));
}
END_TEST

START_TEST(test_quickjs_abort_edits)
{
    ck_assert(exec_script(dom_thread,
        "var c=new AbortController(), t=new EventTarget(), seen=[];"
        "t.addEventListener('x',()=>{seen.push('first');c.abort();});"
        "t.addEventListener('x',()=>seen.push('removed'),{signal:c.signal});t.addEventListener('x',()=>seen.push('last'));"
        "t.dispatchEvent(new Event('x'));if(seen.join()!=='first,last')throw Error('active snapshot '+seen);"
        "var old=new AbortController(), current=new AbortController(), n=0;function f(){n++;}"
        "t.addEventListener('y',f,{signal:old.signal});t.removeEventListener('y',f);t.addEventListener('y',f,{signal:current.signal});"
        "old.abort();t.dispatchEvent(new Event('y'));if(n!==1)throw Error('stale abort algorithm');current.abort();"
        "t.dispatchEvent(new Event('y'));if(n!==1)throw Error('new abort algorithm');"
        "var once=new AbortController();t.addEventListener('once',()=>{once.abort();n++;},{once:true,signal:once.signal});"
        "t.dispatchEvent(new Event('once'));t.dispatchEvent(new Event('once'));if(n!==2)throw Error('once removal');"
        "var self=new AbortController(), hit=false;self.signal.addEventListener('abort',()=>hit=true,{signal:self.signal});"
        "self.abort();if(hit)throw Error('self-signaled listener');"
        "var late=new AbortController();t.addEventListener('late',()=>n++,{get signal(){late.abort();return late.signal;}});"
        "t.dispatchEvent(new Event('late'));if(n!==2)throw Error('abort during conversion');",
        "abort-listener-edits.js"));
}
END_TEST

START_TEST(test_quickjs_abort_gc)
{
    JSMemoryUsage before, after;
    ck_assert(exec_script(dom_thread,
        "function abortCycles(){for(let i=0;i<100;i++){let c=new AbortController(), t=new EventTarget();"
        "t.addEventListener('x',()=>{c;t;},{signal:c.signal});c.signal.onabort=()=>{c;t;};"
        "if(i%2)c.abort(c);}}abortCycles();",
        "abort-cycles.js"));
    JS_RunGC(dom_heap->rt);
    JS_ComputeMemoryUsage(dom_heap->rt, &before);
    ck_assert(exec_script(dom_thread, "abortCycles();", "more abort cycles"));
    JS_RunGC(dom_heap->rt);
    JS_ComputeMemoryUsage(dom_heap->rt, &after);
    ck_assert_msg(after.obj_count <= before.obj_count + 2, "Signal/listener cycles leaked %lld objects",
        (long long)(after.obj_count - before.obj_count));
    ck_assert(exec_script(dom_thread,
        "var retained=new AbortController(), hits=0;{let t=new EventTarget();"
        "t.addEventListener('x',()=>hits++,{signal:retained.signal});retained.signal.addEventListener('check',()=>t.dispatchEvent(new Event('x')));}",
        "retained abort target"));
    JS_RunGC(dom_heap->rt);
    assert_predicate(dom_thread, "(retained.signal.dispatchEvent(new Event('check')),hits===1)", "live abort target retained");
    ck_assert(exec_script(dom_thread, "retained.abort();retained=null;", "release abort target"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_abort_retirement)
{
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retireEvents",
        JS_NewCFunction(dom_thread->ctx, close_from_listener, "retireEvents", 0));
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "var c=new AbortController(), calls=0;c.signal.addEventListener('abort',()=>{calls++;retireEvents();});"
        "c.signal.addEventListener('abort',()=>calls++);document.body.addEventListener('x',()=>calls++,{signal:c.signal});"
        "c.abort();if(calls!==1)throw Error('retired callback ran');",
        "abort-retirement.js"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_abort_timeout)
{
    ck_assert(!exec_script(dom_thread,
        "var c=new AbortController(), t=new EventTarget(), late=false, after=false, e;"
        "t.addEventListener('x',()=>late=true,{signal:c.signal});"
        "c.signal.addEventListener('abort',event=>{e=event;while(true){}});"
        "c.signal.addEventListener('abort',()=>late=true);c.abort();after=true;",
        "abort-timeout.js"));
    assert_predicate(dom_thread,
        "c.signal.aborted&&!late&&!after&&e.currentTarget===null&&e.composedPath().length===0&&"
        "(t.dispatchEvent(new Event('x')),!late)", "abort timeout leaves operations canceled and dispatch cleaned up");
}
END_TEST

START_TEST(test_quickjs_abort_handler)
{
    ck_assert(exec_script(dom_thread,
        "var c=new AbortController(), s=c.signal, seen=[];if(s.onabort!==null)throw Error('initial handler');"
        "s.addEventListener('abort',()=>seen.push('first'));s.onabort=()=>seen.push('old');"
        "s.addEventListener('abort',()=>seen.push('last'));"
        "s.onabort=function(e){if(this!==s||e.currentTarget!==s)throw Error('handler receiver');seen.push('new');return false;};"
        "if(s.dispatchEvent(new Event('abort',{cancelable:true}))||seen.join()!=='first,new,last')throw Error('handler update '+seen);"
        "seen=[];s.onabort=null;s.onabort=()=>{seen.push('readded');return 0;};"
        "if(!s.dispatchEvent(new Event('abort',{cancelable:true}))||seen.join()!=='first,last,readded')throw Error('handler reorder '+seen);"
        "var obj={};s.onabort=obj;if(s.onabort!==obj)throw Error('object handler');"
        "s.dispatchEvent(new Event('abort'));if(seen.length!==5)throw Error('object handler called');"
        "for(let value of [null,undefined,42,'code',true]){s.onabort=value;if(s.onabort!==null)throw Error('primitive handler');}"
        "s.onabort=function(){s.onabort=null;seen.push('self-remove');};c.abort();"
        "if(seen[seen.length-1]!=='self-remove'||s.onabort!==null)throw Error('handler self removal');",
        "abort-handler.js"));
}
END_TEST

START_TEST(test_quickjs_abort_foreign_retirement)
{
    jsthread *source;
    ck_assert_int_eq(js_newthread(dom_heap, NULL, NULL, &source), NSERROR_OK);
    ck_assert(exec_script(source,
        "var c=new AbortController(), t=new EventTarget();t.addEventListener('x',()=>c,{signal:c.signal});"
        "c.signal.onabort=()=>{throw Error('retired handler');};", "foreign-abort-source.js"));
    JSValue global = JS_GetGlobalObject(source->ctx);
    JSValue controller = JS_GetPropertyStr(source->ctx, global, "c");
    JS_FreeValue(source->ctx, global);
    js_destroythread(source);
    JS_RunGC(dom_heap->rt);
    global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retiredController", controller);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "try{AbortController.prototype.abort.call(retiredController);throw Error('accepted');}"
        "catch(e){if(e.name!=='InvalidStateError')throw e;}"
        "var signal=Object.getOwnPropertyDescriptor(AbortController.prototype,'signal').get.call(retiredController);"
        "try{document.addEventListener('x',()=>{},{signal});throw Error('accepted');}"
        "catch(e){if(e.name!=='InvalidStateError')throw e;}"
        "delete globalThis.retiredController;signal=null;", "foreign-abort-retired.js"));
    JS_RunGC(dom_heap->rt);
}
END_TEST

START_TEST(test_quickjs_abort_allocation_failure)
{
    ck_assert(exec_script(dom_thread,
        "function allocateAbort(){let c=new AbortController(), t=new EventTarget(), hit=false;"
        "t.addEventListener('x',()=>hit=true,{signal:c.signal});"
        "c.signal.onabort=()=>t;c.abort();t.dispatchEvent(new Event('x'));"
        "return !hit&&c.signal.aborted&&AbortSignal.abort().aborted;}allocateAbort();", "abort allocation warmup"));
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue callback = JS_GetPropertyStr(dom_thread->ctx, global, "allocateAbort");
    JS_FreeValue(dom_thread->ctx, global);
    bool failed = false, succeeded = false;
    for (size_t extra = 0; extra <= 65536; extra += 128) {
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
    assert_predicate(dom_thread, "allocateAbort()", "abort recovery after allocation failures");
}
END_TEST

START_TEST(test_quickjs_event_allocation_failure)
{
    const char *setup[] = {"function makeTarget(){return new EventTarget();}",
        "function makeTarget(){let root=document.createDocumentFragment(), t=document.createElement('i');"
        "root.appendChild(t);return t;}"};
    ck_assert(exec_script(dom_thread, setup[_i % 2], "event target allocation setup"));
    const char *constructors[] = {"function makeEvent(t){return new CustomEvent('x',{detail:t});}",
        "function makeEvent(t){return new MouseEvent('x',{relatedTarget:t,view:window,clientX:1.25});}",
        "function makeEvent(t){return new SubmitEvent('x',{submitter:document.getElementById('target')});}",
        "function makeEvent(t){return new PointerEvent('x',{pointerType:'pen',relatedTarget:t});}"};
    ck_assert(exec_script(dom_thread, constructors[_i / 2],
        "event allocation constructor"));
    ck_assert(exec_script(dom_thread,
        "function eventAllocation(){let t=makeTarget();let e=makeEvent(t), hit=false;"
        "t.addEventListener('x',()=>{hit=true;},{once:true});t.dispatchEvent(e);return hit;}eventAllocation();",
        "event allocation warmup"));
    JSValue global = JS_GetGlobalObject(dom_thread->ctx);
    JSValue callback = JS_GetPropertyStr(dom_thread->ctx, global, "eventAllocation");
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
    assert_predicate(dom_thread, "eventAllocation()", "event recovery after allocation failures");
}
END_TEST

START_TEST(test_quickjs_event_foreign_retirement)
{
    jsthread *source;
    ck_assert_int_eq(js_newthread(dom_heap, NULL, NULL, &source), NSERROR_OK);
    ck_assert(
        exec_script(source, "var t=new EventTarget();t.addEventListener('x',()=>{throw Error('retired listener');});",
            "retained-event-target.js"));
    JSValue global = JS_GetGlobalObject(source->ctx);
    JSValue retained = JS_GetPropertyStr(source->ctx, global, "t");
    JS_FreeValue(source->ctx, global);
    js_destroythread(source);
    JS_RunGC(dom_heap->rt);
    global = JS_GetGlobalObject(dom_thread->ctx);
    JS_SetPropertyStr(dom_thread->ctx, global, "retiredTarget", retained);
    JS_FreeValue(dom_thread->ctx, global);
    ck_assert(exec_script(dom_thread,
        "try {EventTarget.prototype.dispatchEvent.call(retiredTarget,new Event('x'));throw Error('accepted');}"
        "catch(e){if(e.name!=='InvalidStateError')throw e;}delete globalThis.retiredTarget;",
        "retired-event-target.js"));
    JS_RunGC(dom_heap->rt);
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
     * bounded OOM coverage, not a claim to inject failure at every allocation.
     * CSS property accessors increase document registration beyond 64 KiB. */
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        bool succeeded = false;
        for (size_t extra = 0; extra <= 262144; extra += extra < 65536 ? 256 : 1024) {
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
    tcase_add_test(tc_dom, test_quickjs_node_contains);
    tcase_add_test(tc_dom, test_quickjs_document_default_view);
    tcase_add_test(tc_dom, test_quickjs_node_context_retirement);
    add_style_tests(tc_dom);
    add_clone_tests(tc_dom);
    add_lazy_image_tests(tc_dom);
    add_view_event_tests(tc_dom);
    tcase_add_test(tc_dom, test_quickjs_dom_attributes);
    tcase_add_test(tc_dom, test_quickjs_dom_text);
    tcase_add_test(tc_dom, test_quickjs_dom_conversion);
    tcase_add_loop_test(tc_dom, test_quickjs_inner_html_contexts, 0, sizeof(inner_html_cases) / sizeof(*inner_html_cases));
    tcase_add_test(tc_dom, test_quickjs_inner_html_identity);
    tcase_add_test(tc_dom, test_quickjs_inner_html_conversion);
    tcase_add_test(tc_dom, test_quickjs_inner_html_template);
    tcase_add_test(tc_dom, test_quickjs_inner_html_large);
    tcase_add_test(tc_dom, test_quickjs_inner_html_allocation_failure);
    tcase_add_test(tc_dom, test_quickjs_inner_html_execution_limit);
    tcase_add_test(tc_dom, test_quickjs_inner_html_retirement);
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
    tcase_add_test(tc_dom, test_quickjs_tag_queries);
    tcase_add_test(tc_dom, test_quickjs_tag_queries_live);
    tcase_add_test(tc_dom, test_quickjs_tag_queries_conversion);
    tcase_add_loop_test(tc_dom, test_quickjs_tag_queries_retirement, 0, 4);
    tcase_add_test(tc_dom, test_quickjs_tag_queries_gc);
    tcase_add_test(tc_dom, test_quickjs_node_creation);
    tcase_add_test(tc_dom, test_quickjs_node_creation_errors);
    tcase_add_test(tc_dom, test_quickjs_tree_moves);
    tcase_add_test(tc_dom, test_quickjs_tree_fragments);
    tcase_add_test(tc_dom, test_quickjs_tree_validation);
    tcase_add_test(tc_dom, test_quickjs_document_tree_validation);
    tcase_add_test(tc_dom, test_quickjs_creation_retirement);
    tcase_add_test(tc_dom, test_quickjs_child_collections);
    tcase_add_test(tc_dom, test_quickjs_collections_live);
    tcase_add_test(tc_dom, test_quickjs_collection_names);
    tcase_add_test(tc_dom, test_quickjs_collection_properties);
    tcase_add_test(tc_dom, test_quickjs_collection_prototypes);
    tcase_add_test(tc_dom, test_quickjs_collection_iteration);
    tcase_add_test(tc_dom, test_quickjs_collection_errors);
    tcase_add_test(tc_dom, test_quickjs_collection_retirement);
    tcase_add_loop_test(tc_dom, test_quickjs_collection_allocation_failure, 0, 2);
    tcase_add_loop_test(tc_dom, test_quickjs_collection_conversion_retirement, 0, 3);
    tcase_add_test(tc_dom, test_quickjs_event_values);
    tcase_add_test(tc_dom, test_quickjs_event_conversion);
    tcase_add_test(tc_dom, test_quickjs_event_dispatch);
    tcase_add_test(tc_dom, test_quickjs_event_listener_identity);
    tcase_add_test(tc_dom, test_quickjs_event_listener_edits);
    tcase_add_test(tc_dom, test_quickjs_event_stop_and_passive);
    tcase_add_test(tc_dom, test_quickjs_event_callback_errors);
    tcase_add_test(tc_dom, test_quickjs_event_microtasks);
    tcase_add_test(tc_dom, test_quickjs_event_timeout);
    tcase_add_test(tc_dom, test_quickjs_event_gc);
    tcase_add_test(tc_dom, test_quickjs_event_retirement);
    tcase_add_loop_test(tc_dom, test_quickjs_event_conversion_retirement, 0, 2);
    tcase_add_loop_test(tc_dom, test_quickjs_event_allocation_failure, 0, 8);
    tcase_add_test(tc_dom, test_quickjs_event_foreign_retirement);
    tcase_add_test(tc_dom, test_quickjs_node_event_path);
    tcase_add_test(tc_dom, test_quickjs_node_event_frozen_path);
    tcase_add_test(tc_dom, test_quickjs_node_event_roots);
    tcase_add_test(tc_dom, test_quickjs_node_event_passive);
    tcase_add_test(tc_dom, test_quickjs_node_event_nested);
    tcase_add_test(tc_dom, test_quickjs_node_event_native_guard);
    tcase_add_test(tc_dom, test_quickjs_node_event_retirement);
    tcase_add_test(tc_dom, test_quickjs_node_event_conversion_retirement);
    tcase_add_test(tc_dom, test_quickjs_node_event_long_path);
    tcase_add_test(tc_dom, test_quickjs_ui_event_values);
    tcase_add_test(tc_dom, test_quickjs_mouse_event_values);
    tcase_add_test(tc_dom, test_quickjs_input_event_conversion);
    tcase_add_test(tc_dom, test_quickjs_mouse_event_modifiers);
    tcase_add_test(tc_dom, test_quickjs_input_event_initializers);
    tcase_add_test(tc_dom, test_quickjs_input_event_dispatch);
    tcase_add_test(tc_dom, test_quickjs_input_event_gc);
    tcase_add_test(tc_dom, test_quickjs_input_event_retirement);
    tcase_add_test(tc_dom, test_quickjs_input_event_conversion_retirement);
    tcase_add_test(tc_dom, test_quickjs_document_readiness);
    tcase_add_test(tc_dom, test_quickjs_document_lifecycle);
    tcase_add_test(tc_dom, test_quickjs_document_lifecycle_allocation_failure);
    tcase_add_test(tc_dom, test_quickjs_native_submit);
    tcase_add_test(tc_dom, test_quickjs_native_submit_without_button);
    tcase_add_test(tc_dom, test_quickjs_native_submit_allocation_failure);
    tcase_add_test(tc_dom, test_quickjs_native_click);
    tcase_add_test(tc_dom, test_quickjs_native_click_defaults);
    tcase_add_test(tc_dom, test_quickjs_native_click_mutation);
    tcase_add_test(tc_dom, test_quickjs_native_click_exception);
    tcase_add_test(tc_dom, test_quickjs_native_click_retirement);
    tcase_add_test(tc_dom, test_quickjs_native_click_timeout);
    tcase_add_test(tc_dom, test_quickjs_pointer_event);
    tcase_add_test(tc_dom, test_quickjs_native_click_allocation_failure);
    tcase_add_test(tc_dom, test_quickjs_native_click_shared_budget);
    tcase_add_test(tc_dom, test_quickjs_submit_event_values);
    tcase_add_test(tc_dom, test_quickjs_submit_event_brands);
    tcase_add_test(tc_dom, test_quickjs_submit_event_conversion);
    tcase_add_test(tc_dom, test_quickjs_submit_event_dispatch);
    tcase_add_test(tc_dom, test_quickjs_submit_event_gc);
    tcase_add_test(tc_dom, test_quickjs_submit_event_retirement);
    tcase_add_loop_test(tc_dom, test_quickjs_event_constructor_retirement, 0,
        EVENT_CONSTRUCTOR_KINDS * EVENT_CONVERSION_STAGES);
    tcase_add_test(tc_dom, test_quickjs_abort_values);
    tcase_add_test(tc_dom, test_quickjs_abort_foreign_retirement);
    tcase_add_test(tc_dom, test_quickjs_abort_handler);
    tcase_add_test(tc_dom, test_quickjs_abort_event);
    tcase_add_test(tc_dom, test_quickjs_abort_static);
    tcase_add_test(tc_dom, test_quickjs_abort_listeners);
    tcase_add_test(tc_dom, test_quickjs_abort_edits);
    tcase_add_test(tc_dom, test_quickjs_abort_gc);
    tcase_add_test(tc_dom, test_quickjs_abort_retirement);
    tcase_add_test(tc_dom, test_quickjs_abort_timeout);
    tcase_add_test(tc_dom, test_quickjs_abort_allocation_failure);
    tcase_add_test(tc_dom, test_quickjs_window_events);
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
