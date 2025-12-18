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
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "content/handlers/javascript/js.h"

/* Include QuickJS directly for console binding tests */
#include "quickjs.h"
#include "content/handlers/javascript/quickjs/console.h"

/**
 * Test that js_initialise and js_finalise work without crashing.
 */
START_TEST(test_quickjs_init_finalise)
{
	js_initialise();
	js_finalise();
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
	result = js_exec(
		thread, (const uint8_t *)code, strlen(code), "test_error");
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
	result = js_exec(
		thread, (const uint8_t *)code, strlen(code), "test_objects");
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
 * Create the test suite.
 */
static Suite *quickjs_suite(void)
{
	Suite *s;
	TCase *tc_core;
	TCase *tc_exec;
	TCase *tc_console;

	s = suite_create("QuickJS");

	/* Core test case */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, test_quickjs_init_finalise);
	tcase_add_test(tc_core, test_quickjs_heap_create_destroy);
	tcase_add_test(tc_core, test_quickjs_thread_create_destroy);
	tcase_add_test(tc_core, test_quickjs_multiple_threads);
	suite_add_tcase(s, tc_core);

	/* Execution test case */
	tc_exec = tcase_create("Execution");
	tcase_add_test(tc_exec, test_quickjs_exec_simple);
	tcase_add_test(tc_exec, test_quickjs_exec_syntax_error);
	tcase_add_test(tc_exec, test_quickjs_exec_objects);
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
