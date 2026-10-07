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

/**
 * Test Timers (stubs).
 */
START_TEST(test_quickjs_timers)
{
    jsheap *heap = NULL;
    jsthread *thread = NULL;
    nserror err;

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test setTimeout exists and returns a number */
    const char *code1 = "typeof setTimeout === 'function'";
    assert_predicate(thread, code1, "test_setTimeout");

    /* Test clearTimeout exists */
    const char *code2 = "typeof clearTimeout === 'function'";
    assert_predicate(thread, code2, "test_clearTimeout");

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

    js_initialise();

    err = js_newheap(5, &heap);
    ck_assert_int_eq(err, NSERROR_OK);

    err = js_newthread(heap, NULL, NULL, &thread);
    ck_assert_int_eq(err, NSERROR_OK);

    /* Test document exists */
    const char *code1 = "typeof document === 'object' && typeof window.document === 'object'";
    assert_predicate(thread, code1, "test_document1");

    /* Test getElementById stub */
    const char *code2 = "document.getElementById('foo') === null";
    assert_predicate(thread, code2, "test_getElementById");

    /* Test createElement stub */
    const char *code3 = "typeof document.createElement('div') === 'object'";
    assert_predicate(thread, code3, "test_createElement");

    /* Test write stub */
    const char *code4 = "typeof document.write === 'function'";
    assert_predicate(thread, code4, "test_write");

    /* Test body and documentElement properties */
    const char *code5 = "typeof document.body === 'object' && typeof document.documentElement === 'object'";
    assert_predicate(thread, code5, "test_doc_props");

    /* Test cookie */
    const char *code6 = "document.cookie === ''";
    assert_predicate(thread, code6, "test_cookie");

    /* Test element.style property exists */
    const char *code7 = "typeof document.createElement('div').style === 'object'";
    assert_predicate(thread, code7, "test_element_style");

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

    /* These assert the current stub surface, not Web API conformance. Replace
     * them with behavioral tests as real implementations replace the stubs. */
    tc_surface = tcase_create("Stub API surface (not conformance)");
    tcase_add_test(tc_surface, test_quickjs_unsupported_dialogs);
    tcase_add_test(tc_surface, test_quickjs_timers);
    tcase_add_test(tc_surface, test_quickjs_navigator);
    tcase_add_test(tc_surface, test_quickjs_location);
    tcase_add_test(tc_surface, test_quickjs_document);
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
