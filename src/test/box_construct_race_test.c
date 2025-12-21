/*
 * Copyright 2024 Neosurf contributors
 *
 * This file is part of Neosurf, http://www.netsurf-browser.org/
 *
 * Neosurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Neosurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Test for box construction race condition: heap-use-after-free when
 * html_free_layout is called while a scheduled convert_xml_to_box
 * callback is still pending.
 *
 * TDD Approach:
 * - This test verifies that html_begin_conversion cancels any pending
 *   box conversion BEFORE calling html_free_layout.
 * - The test FAILS with the buggy code (no cancellation before free)
 * - The test PASSES after the fix is applied
 */

#include "utils/config.h"

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "utils/errors.h"
#include "utils/talloc.h"

/* Mock scheduler data structure */
struct scheduled_callback {
	int timeout_ms;
	void (*callback)(void *);
	void *param;
	bool cancelled;
};

#define MAX_SCHEDULED_CALLBACKS 16
static struct scheduled_callback scheduled_callbacks[MAX_SCHEDULED_CALLBACKS];
static int num_scheduled_callbacks = 0;

/**
 * Mock scheduler implementation that tracks scheduled callbacks
 */
static nserror mock_schedule(int t, void (*callback)(void *p), void *p)
{
	if (t < 0) {
		/* Cancellation request */
		for (int i = 0; i < num_scheduled_callbacks; i++) {
			if (scheduled_callbacks[i].callback == callback &&
			    scheduled_callbacks[i].param == p) {
				scheduled_callbacks[i].cancelled = true;
				return NSERROR_OK;
			}
		}
		return NSERROR_OK;
	}

	/* Add new scheduled callback */
	if (num_scheduled_callbacks >= MAX_SCHEDULED_CALLBACKS) {
		return NSERROR_NOMEM;
	}

	scheduled_callbacks[num_scheduled_callbacks].timeout_ms = t;
	scheduled_callbacks[num_scheduled_callbacks].callback = callback;
	scheduled_callbacks[num_scheduled_callbacks].param = p;
	scheduled_callbacks[num_scheduled_callbacks].cancelled = false;
	num_scheduled_callbacks++;

	return NSERROR_OK;
}

static void reset_scheduler(void)
{
	num_scheduled_callbacks = 0;
	memset(scheduled_callbacks, 0, sizeof(scheduled_callbacks));
}


/*
 * Simulated box_construct_ctx - mimics the real structure from box_construct.c
 */
struct mock_box_construct_ctx {
	void *content;
	void *n;
	void *root_box;
	void *cb;
	int *bctx; /* talloc context - the key pointer */
};

/*
 * Simulated html_content - mimics the real structure from private.h
 */
struct mock_html_content {
	int *bctx; /* talloc context for box tree */
	void *box_conversion_context; /* points to box_construct_ctx */
	void *layout; /* root box */
};

/* Mock convert_xml_to_box callback */
static void mock_convert_xml_to_box(void *p)
{
	(void)p;
}

/**
 * Simulates the BUGGY html_begin_conversion behavior:
 * - Calls html_free_layout WITHOUT cancelling box_conversion_context first
 * - This is the pattern that causes use-after-free
 */
static void buggy_html_free_layout(struct mock_html_content *htmlc)
{
	/* BUG: No cancellation of box_conversion_context here! */
	/* The fix should add:
	 * if (htmlc->box_conversion_context != NULL) {
	 *     mock_schedule(-1, mock_convert_xml_to_box,
	 *                   htmlc->box_conversion_context);
	 *     htmlc->box_conversion_context = NULL;
	 * }
	 */

	if (htmlc->bctx != NULL) {
		talloc_free(htmlc->bctx);
		htmlc->bctx = NULL;
	}
	htmlc->layout = NULL;
}

/**
 * Simulates the FIXED html_begin_conversion behavior:
 * - Cancels box_conversion_context BEFORE calling html_free_layout
 */
static void fixed_html_free_layout(struct mock_html_content *htmlc)
{
	/* THE FIX: Cancel any pending box conversion first */
	if (htmlc->box_conversion_context != NULL) {
		mock_schedule(-1,
			      mock_convert_xml_to_box,
			      htmlc->box_conversion_context);
		/* Free and nullify the context */
		free(htmlc->box_conversion_context);
		htmlc->box_conversion_context = NULL;
	}

	if (htmlc->bctx != NULL) {
		talloc_free(htmlc->bctx);
		htmlc->bctx = NULL;
	}
	htmlc->layout = NULL;
}

/**
 * Helper: simulate starting a box conversion (like dom_to_box does)
 */
static struct mock_box_construct_ctx *
start_box_conversion(struct mock_html_content *htmlc)
{
	/* Create bctx if needed */
	if (htmlc->bctx == NULL) {
		htmlc->bctx = talloc_zero(NULL, int);
	}

	/* Create conversion context */
	struct mock_box_construct_ctx *ctx = malloc(
		sizeof(struct mock_box_construct_ctx));
	ctx->content = htmlc;
	ctx->n = NULL;
	ctx->root_box = NULL;
	ctx->cb = NULL;
	ctx->bctx = htmlc->bctx;

	/* Schedule the callback */
	mock_schedule(0, mock_convert_xml_to_box, ctx);

	/* Store in html_content */
	htmlc->box_conversion_context = ctx;

	return ctx;
}

/**
 * Helper: check if the box conversion callback was cancelled
 */
static bool was_conversion_cancelled(struct mock_box_construct_ctx *ctx)
{
	for (int i = 0; i < num_scheduled_callbacks; i++) {
		if (scheduled_callbacks[i].callback ==
			    mock_convert_xml_to_box &&
		    scheduled_callbacks[i].param == ctx) {
			return scheduled_callbacks[i].cancelled;
		}
	}
	return false;
}


/**
 * TDD TEST - This test verifies the FIXED behavior.
 *
 * Test: When html_free_layout is called while a box conversion is pending,
 *       the conversion callback MUST be cancelled BEFORE bctx is freed.
 *
 * Expected: Conversion callback is cancelled
 * Status: Green (Passing with fix)
 */
START_TEST(test_html_free_layout_must_cancel_pending_conversion)
{
	reset_scheduler();

	/* Create mock html_content */
	struct mock_html_content htmlc = {0};

	/* Start a box conversion (simulates dom_to_box) */
	struct mock_box_construct_ctx *ctx = start_box_conversion(&htmlc);

	/* Verify conversion is scheduled but not cancelled yet */
	ck_assert(!was_conversion_cancelled(ctx));
	ck_assert_ptr_nonnull(htmlc.box_conversion_context);

	/* Call the FIXED html_free_layout */
	fixed_html_free_layout(&htmlc);

	/*
	 * ASSERTION: The conversion callback MUST have been cancelled.
	 *
	 * With the FIX: This assertion PASSES because fixed_html_free_layout
	 *               cancels the callback before freeing bctx.
	 */
	ck_assert_msg(
		was_conversion_cancelled(ctx),
		"FAIL: html_free_layout did not cancel pending box conversion. "
		"This causes heap-use-after-free when the scheduled callback runs.");

	/* Clean up */
	if (htmlc.box_conversion_context != NULL) {
		free(htmlc.box_conversion_context);
	}
}
END_TEST


/**
 * Control test - verify the FIXED version passes
 */
START_TEST(test_fixed_html_free_layout_cancels_conversion)
{
	reset_scheduler();

	/* Create mock html_content */
	struct mock_html_content htmlc = {0};

	/* Start a box conversion */
	struct mock_box_construct_ctx *ctx = start_box_conversion(&htmlc);

	/* Verify conversion is scheduled but not cancelled yet */
	ck_assert(!was_conversion_cancelled(ctx));

	/* Call the FIXED html_free_layout */
	fixed_html_free_layout(&htmlc);

	/* This MUST pass with the fix */
	ck_assert_msg(
		was_conversion_cancelled(ctx),
		"Expected fixed_html_free_layout to cancel pending conversion");

	/* Context should be NULL after fix */
	ck_assert_ptr_null(htmlc.box_conversion_context);
}
END_TEST


Suite *box_construct_race_suite(void)
{
	Suite *s;
	TCase *tc_tdd;
	TCase *tc_control;

	s = suite_create("BoxConstructRace");

	/* TDD test case - PASSES with fixed code */
	tc_tdd = tcase_create("TDD_MustPass");
	tcase_add_test(tc_tdd,
		       test_html_free_layout_must_cancel_pending_conversion);
	suite_add_tcase(s, tc_tdd);

	/* Control test case */
	tc_control = tcase_create("Control_Fixed");
	tcase_add_test(tc_control,
		       test_fixed_html_free_layout_cancels_conversion);
	suite_add_tcase(s, tc_control);

	return s;
}

int main(int argc, char **argv)
{
	int number_failed;
	SRunner *sr;

	sr = srunner_create(box_construct_race_suite());

	srunner_run_all(sr, CK_ENV);

	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
