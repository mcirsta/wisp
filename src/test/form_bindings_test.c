/* Real DOM ownership, gadgets and box retirement; no UI widgets are created. */
#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdom/bindings/hubbub/parser.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/form_internal.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/form_bindings.h"
#include "content/handlers/html/box_manipulate.h"
#include "utils/talloc.h"
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

enum { BEFORE, REPLACE, REMOVE, APPEAR, AFTER, SECOND, SECOND_NEW, OTHER, FORMLESS, UNTOUCHED, CONTROL_COUNT };
static const char *ids[] = {
    "before", "replace", "remove", "appear", "after", "second", "second-new", "other", "formless", "untouched"};
static const unsigned old_ids[] = {REPLACE, REMOVE, SECOND, FORMLESS};
static const unsigned new_ids[] = {REPLACE, APPEAR, SECOND, SECOND_NEW, FORMLESS};
static html_content html;
static void *arena;
static struct box *old_root, *new_root;
static dom_node *nodes[CONTROL_COUNT];
static struct box *live[CONTROL_COUNT], *fresh[CONTROL_COUNT];

static dom_node *by_id(const char *id)
{
    dom_string *key;
    dom_node *node = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)id, strlen(id), &key), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(html.document, key, &node), DOM_NO_ERR);
    dom_string_unref(key);
    ck_assert_ptr_nonnull(node);
    return node;
}

static struct box *root(void)
{
    struct box *box = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, arena);
    ck_assert_ptr_nonnull(box);
    box->type = BOX_BLOCK;
    return box;
}

static struct box *control_box(unsigned id, struct box *parent, bool detached)
{
    struct box *box = box_create(NULL, NULL, false, NULL, NULL, NULL, NULL, parent == NULL ? arena : parent);
    ck_assert_ptr_nonnull(box);
    box->type = BOX_INLINE_BLOCK;
    box->node = dom_node_ref(nodes[id]);
    if (detached) {
        ck_assert_int_eq(html_forms_create_detached_control(nodes[id], &box->gadget), NSERROR_OK);
    } else {
        box->gadget = html_forms_get_control_for_node(html.forms, nodes[id]);
        ck_assert_ptr_nonnull(box->gadget);
    }
    box->gadget->box = box;
    box->gadget->html = &html;
    if (parent != NULL)
        box_add_child(parent, box);
    return box;
}

static void create_candidate(void)
{
    new_root = root();
    memset(fresh, 0, sizeof(fresh));
    for (unsigned i = 0; i < sizeof(new_ids) / sizeof(new_ids[0]); i++)
        fresh[new_ids[i]] = control_box(new_ids[i], new_root, true);
}

static void setup(void)
{
    memset(&html, 0, sizeof(html));
    memset(live, 0, sizeof(live));
    const char *markup = "<form id=main><input id=before type=text value=untouched>"
                         "<input id=replace type=radio name=tabs checked><input id=remove type=checkbox>"
                         "<input id=appear type=radio name=tabs><button id=after>Outside button</button></form>"
                         "<form id=other-form><input id=second type=checkbox checked>"
                         "<input id=second-new type=button value=New><input id=other type=checkbox></form>"
                         "<input id=formless type=checkbox><form id=third><input id=untouched type=radio></form>";
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &html.document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)markup, strlen(markup)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    html.forms = html_forms_get_forms((dom_html_document *)html.document);
    ck_assert_ptr_nonnull(html.forms);
    arena = talloc_new(NULL);
    ck_assert_ptr_nonnull(arena);
    for (unsigned i = 0; i < CONTROL_COUNT; i++)
        nodes[i] = by_id(ids[i]);
    old_root = root();
    for (unsigned i = 0; i < CONTROL_COUNT; i++) {
        if (i == APPEAR || i == SECOND_NEW)
            continue;
        bool inside = false;
        for (unsigned j = 0; j < sizeof(old_ids) / sizeof(old_ids[0]); j++)
            inside |= i == old_ids[j];
        live[i] = control_box(i, inside ? old_root : NULL, false);
    }
    create_candidate();
}

static void teardown(void)
{
    talloc_free(arena); /* Gadgets unlink before forms and DOM are destroyed. */
    while (html.forms != NULL) {
        struct form *prev = html.forms->prev;
        form_free(html.forms);
        html.forms = prev;
    }
    for (unsigned i = 0; i < CONTROL_COUNT; i++)
        dom_node_unref(nodes[i]);
    dom_node_unref(html.document);
}

/* Include links and lookup identities as well as values, without mutating the
 * live lookup cache (the ordinary getter can create a missing gadget). */
static char *snapshot(void)
{
    char *buffer;
    size_t length;
    FILE *out = open_memstream(&buffer, &length);
    ck_assert_ptr_nonnull(out);
    for (struct form *f = html.forms; f != NULL; f = f->prev) {
        fprintf(out, "form=%p head=%p last=%p index=%p\n", (void *)f, (void *)f->controls, (void *)f->last_control,
            (void *)f->control_index);
        for (struct form_control *c = f->controls; c != NULL; c = c->next) {
            fprintf(out, "control=%p form=%p prev=%p next=%p box=%p value=%s selected=%d\n", (void *)c, (void *)c->form,
                (void *)c->prev, (void *)c->next, (void *)c->box, c->value == NULL ? "" : c->value, c->selected);
        }
        if (f->control_index != NULL) {
            fprintf(out, "entries=%zu\n", hashmap_count(f->control_index));
            for (unsigned i = 0; i < CONTROL_COUNT; i++) {
                void **slot = hashmap_lookup(f->control_index, nodes[i]);
                fprintf(out, "%s=%p\n", ids[i], slot == NULL ? NULL : *slot);
            }
        }
    }
    ck_assert_int_eq(fclose(out), 0);
    return buffer;
}

static void unchanged(const char *before)
{
    char *after = snapshot();
    ck_assert_str_eq(after, before);
    free(after);
}

static void lock_publication(void)
{
    html.base.locked = true;
    html.restyling = true;
}

static void check_form(struct form *form, struct form_control **expected, size_t count)
{
    struct form_control *c = form->controls;
    for (size_t i = 0; i < count; i++) {
        ck_assert_ptr_eq(c, expected[i]);
        ck_assert_ptr_eq(c->form, form);
        ck_assert_ptr_eq(c->prev, i == 0 ? NULL : expected[i - 1]);
        if (form->control_index != NULL) {
            void **slot = hashmap_lookup(form->control_index, c->node);
            ck_assert_ptr_nonnull(slot);
            ck_assert_ptr_eq(*slot, c);
        }
        c = c->next;
    }
    ck_assert_ptr_null(c);
    ck_assert_ptr_eq(form->last_control, count == 0 ? NULL : expected[count - 1]);
    if (form->control_index != NULL)
        ck_assert_uint_eq(hashmap_count(form->control_index), count);
}

START_TEST(test_apply_and_retire)
{
    struct form *main = live[REPLACE]->gadget->form, *other = live[SECOND]->gadget->form;
    if (_i == 1) { /* The pre-existing list-only fallback remains supported. */
        hashmap_destroy(main->control_index);
        main->control_index = NULL;
    }
    struct form unaffected = *live[UNTOUCHED]->gadget->form;
    struct form_control untouched = *live[UNTOUCHED]->gadget;
    char *value = live[BEFORE]->gadget->value;
    struct html_form_binding_plan *plan;
    char *before = snapshot();
    ck_assert_int_eq(html_form_prepare_bindings(&html, old_root, new_root, arena, &plan), NSERROR_OK);
    unchanged(before);
    ck_assert_int_eq(html_form_check_bindings(plan), NSERROR_OK);
    ck_assert_int_eq(html_form_apply_bindings(plan), NSERROR_INVALID); /* Publication must be excluded. */
    unchanged(before);
    free(before);
    lock_publication();
    ck_assert_int_eq(html_form_apply_bindings(plan), NSERROR_OK);
    ck_assert_int_eq(html_form_apply_bindings(plan), NSERROR_INVALID);
    for (unsigned i = 0; i < sizeof(old_ids) / sizeof(old_ids[0]); i++) {
        struct form_control *c = live[old_ids[i]]->gadget;
        ck_assert_ptr_null(c->form);
        ck_assert_ptr_null(c->prev);
        ck_assert_ptr_null(c->next);
    }
    struct form_control *main_controls[] = {
        live[BEFORE]->gadget, fresh[REPLACE]->gadget, live[AFTER]->gadget, fresh[APPEAR]->gadget};
    struct form_control *other_controls[] = {fresh[SECOND]->gadget, live[OTHER]->gadget, fresh[SECOND_NEW]->gadget};
    check_form(main, main_controls, 4);
    check_form(other, other_controls, 3);
    ck_assert_ptr_null(fresh[FORMLESS]->gadget->form);
    ck_assert_ptr_eq(live[BEFORE]->gadget->value, value);
    ck_assert_mem_eq(live[UNTOUCHED]->gadget, &untouched, sizeof(untouched));
    ck_assert_mem_eq(live[UNTOUCHED]->gadget->form, &unaffected, sizeof(unaffected));
    html_form_discard_bindings(plan);
    talloc_free(old_root); /* Old destructors must not erase new registrations. */
    old_root = NULL;
    check_form(main, main_controls, 4);
    check_form(other, other_controls, 3);
    ck_assert_ptr_eq(html_forms_get_control_for_node(html.forms, nodes[REPLACE]), fresh[REPLACE]->gadget);
    if (_i == 1)
        ck_assert_ptr_null(main->control_index);
}
END_TEST

START_TEST(test_discard_and_stale)
{
    struct html_form_binding_plan *plan;
    ck_assert_int_eq(html_form_prepare_bindings(&html, old_root, new_root, arena, &plan), NSERROR_OK);
    struct form *saved_forms = html.forms;
    struct form_control *c = live[SECOND]->gadget;
    struct box *saved_box = c->box;
    hashmap_t *index = c->form->control_index;
    switch (_i) {
    case 0:
        break; /* Cancellation after successful preparation. */
    case 1:
        html.forms = html.forms->prev;
        break;
    case 2:
        c->form->control_index = NULL;
        break;
    case 3:
        c->box = new_root;
        break;
    case 4:
        form_free_control(c);
        break;
    case 5:
        fresh[SECOND]->gadget->form = live[OTHER]->gadget->form;
        break;
    case 6:
        fresh[SECOND]->gadget->prev = live[OTHER]->gadget;
        break;
    case 7:
        c->form->last_control = c;
        break;
    }
    char *before = snapshot();
    if (_i != 0) {
        lock_publication();
        ck_assert_int_eq(html_form_check_bindings(plan), NSERROR_INVALID);
        ck_assert_int_eq(html_form_apply_bindings(plan), NSERROR_INVALID);
    }
    html_form_discard_bindings(plan);
    unchanged(before);
    free(before);
    html.forms = saved_forms;
    if (_i == 2)
        c->form->control_index = index;
    if (_i == 3)
        c->box = saved_box;
    if (_i == 5)
        fresh[SECOND]->gadget->form = NULL;
    if (_i == 6)
        fresh[SECOND]->gadget->prev = NULL;
    if (_i == 7)
        c->form->last_control = live[OTHER]->gadget;
}
END_TEST

START_TEST(test_reject_unsupported)
{
    struct form_control *c = _i < 3 ? live[SECOND]->gadget : fresh[SECOND]->gadget;
    form_control_type saved = c->type;
    const form_control_type unsupported[] = {GADGET_TEXTBOX, GADGET_SELECT, GADGET_FILE};
    c->type = unsupported[_i % 3];
    char *before = snapshot();
    size_t blocks = talloc_total_blocks(arena);
    struct html_form_binding_plan *plan;
    ck_assert_int_eq(html_form_prepare_bindings(&html, old_root, new_root, arena, &plan), NSERROR_NOT_IMPLEMENTED);
    ck_assert_ptr_null(plan);
    unchanged(before);
    ck_assert_uint_eq(talloc_total_blocks(arena), blocks);
    free(before);
    c->type = saved;
}
END_TEST

START_TEST(test_reject_unknown_owner)
{
    struct form *forms = html.forms;
    html.forms = NULL; /* A new/unregistered DOM form cannot be invented at commit. */
    struct html_form_binding_plan *plan;
    ck_assert_int_eq(html_form_prepare_bindings(&html, NULL, new_root, arena, &plan), NSERROR_NOT_IMPLEMENTED);
    ck_assert_ptr_null(plan);
    html.forms = forms;
}
END_TEST

START_TEST(test_hide_show_plateau)
{
    talloc_free(new_root);
    new_root = NULL;
    struct form *main = live[REPLACE]->gadget->form, *other = live[SECOND]->gadget->form;
    size_t steady_blocks = 0;
    lock_publication();
    for (unsigned pass = 0; pass < 100; pass++) {
        struct html_form_binding_plan *plan;
        ck_assert_int_eq(html_form_prepare_bindings(&html, old_root, NULL, arena, &plan), NSERROR_OK);
        ck_assert_int_eq(html_form_apply_bindings(plan), NSERROR_OK);
        html_form_discard_bindings(plan);
        talloc_free(old_root);
        old_root = NULL;
        struct form_control *main_hidden[] = {live[BEFORE]->gadget, live[AFTER]->gadget};
        struct form_control *other_hidden[] = {live[OTHER]->gadget};
        check_form(main, main_hidden, 2);
        check_form(other, other_hidden, 1);
        create_candidate();
        ck_assert_int_eq(html_form_prepare_bindings(&html, NULL, new_root, arena, &plan), NSERROR_OK);
        ck_assert_int_eq(html_form_apply_bindings(plan), NSERROR_OK);
        html_form_discard_bindings(plan);
        struct form_control *main_visible[] = {
            live[BEFORE]->gadget, live[AFTER]->gadget, fresh[REPLACE]->gadget, fresh[APPEAR]->gadget};
        struct form_control *other_visible[] = {live[OTHER]->gadget, fresh[SECOND]->gadget, fresh[SECOND_NEW]->gadget};
        check_form(main, main_visible, 4);
        check_form(other, other_visible, 3);
        old_root = new_root;
        new_root = NULL;
        if (pass == 0)
            steady_blocks = talloc_total_blocks(arena);
        ck_assert_uint_eq(talloc_total_blocks(arena), steady_blocks);
    }
}
END_TEST

START_TEST(test_unlink_repairs_neighbors)
{
    struct form *form = live[REPLACE]->gadget->form;
    unsigned victim[] = {BEFORE, REPLACE, AFTER};
    form_free_control(live[victim[_i]]->gadget);
    struct form_control *expected[4];
    size_t count = 0;
    const unsigned order[] = {BEFORE, REPLACE, REMOVE, AFTER};
    for (unsigned i = 0; i < 4; i++)
        if (order[i] != victim[_i])
            expected[count++] = live[order[i]]->gadget;
    check_form(form, expected, count);
}
END_TEST

START_TEST(test_retirement_preserves_replacement_index)
{
    struct form_control *old = live[REPLACE]->gadget, *replacement = fresh[REPLACE]->gadget;
    struct form *form = old->form;
    void **slot = hashmap_lookup(form->control_index, old->node);
    ck_assert_ptr_nonnull(slot);
    *slot = replacement;
    form_free_control(old);
    slot = hashmap_lookup(form->control_index, replacement->node);
    ck_assert_ptr_nonnull(slot);
    ck_assert_ptr_eq(*slot, replacement);
    /* This test exercises retirement independently of list publication. */
    hashmap_remove(form->control_index, replacement->node);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_failures)
{
    char *before = snapshot();
    size_t blocks = talloc_total_blocks(arena);
    struct html_form_binding_plan *plan = NULL;
    unsigned fail_after;
    for (fail_after = 0; fail_after < 128; fail_after++) {
        malloc_limit(fail_after);
        nserror err = html_form_prepare_bindings(&html, old_root, new_root, arena, &plan);
        malloc_limit(UINT_MAX);
        unchanged(before);
        if (err == NSERROR_OK)
            break;
        ck_assert_msg(err == NSERROR_NOMEM, "Failure %u returned %d", fail_after, err);
        ck_assert_ptr_null(plan);
        ck_assert_uint_eq(talloc_total_blocks(arena), blocks);
    }
    ck_assert_uint_lt(fail_after, 128);
    ck_assert_uint_gt(fail_after, 10); /* Includes failures partway through new indexes. */
    free(before);
    lock_publication();
    malloc_limit(0);
    realloc_limit(0);
    nserror err = html_form_apply_bindings(plan);
    html_form_discard_bindings(plan);
    talloc_free(old_root);
    old_root = NULL;
    malloc_limit(UINT_MAX);
    realloc_limit(UINT_MAX);
    ck_assert_int_eq(err, NSERROR_OK);
    ck_assert_ptr_eq(html_forms_get_control_for_node(html.forms, nodes[REPLACE]), fresh[REPLACE]->gadget);
}
END_TEST
#endif

int main(void)
{
    if (corestrings_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Prepared form bindings");
    TCase *tc = tcase_create("Registration and retirement");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_apply_and_retire, 0, 2);
    tcase_add_loop_test(tc, test_discard_and_stale, 0, 8);
    tcase_add_loop_test(tc, test_reject_unsupported, 0, 6);
    tcase_add_test(tc, test_reject_unknown_owner);
    tcase_add_test(tc, test_hide_show_plateau);
    tcase_add_loop_test(tc, test_unlink_repairs_neighbors, 0, 3);
    tcase_add_test(tc, test_retirement_preserves_replacement_index);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_allocation_failures);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
