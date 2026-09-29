/* Class hash growth: parser integration, lookup order, storage and OOM. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_allocation = -1;
static int allocation_count;

static void *test_calloc(size_t count, size_t size)
{
    if (allocation_count++ == fail_allocation)
        return NULL;
    return calloc(count, size);
}

/* Inspect private buckets and fail only hash allocations, without adding test
 * hooks to the library. Rename the copy so parsing still uses the real API. */
#define css__selector_hash_create test_hash_create
#define css__selector_hash_destroy test_hash_destroy
#define css__selector_hash_finish test_hash_finish
#define css__selector_hash_insert test_hash_insert
#define css__selector_hash_remove test_hash_remove
#define css__selector_hash_find test_hash_find
#define css__selector_hash_find_by_class test_hash_find_by_class
#define css__selector_hash_find_by_id test_hash_find_by_id
#define css__selector_hash_find_universal test_hash_find_universal
#define css__selector_hash_size test_hash_size
#define calloc test_calloc
#include "../src/select/hash.c"
#undef calloc

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    (void)pw;
    (void)base;
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static void append_css(css_stylesheet *sheet, const char *text)
{
    css_error error = css_stylesheet_append_data(sheet, (const uint8_t *)text, strlen(text));
    assert(error == CSS_OK || error == CSS_NEEDDATA);
}

static css_stylesheet *make_sheet(unsigned count, bool mixed, bool pending_import)
{
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_3,
        .charset = "UTF-8",
        .url = "selector-hash",
        .resolve = resolve_url,
    };
    css_stylesheet *sheet;
    assert(css_stylesheet_create(&params, &sheet) == CSS_OK);
    if (pending_import)
        append_css(sheet, "@import 'pending.css';");
    for (unsigned i = 0; i < count; i++) {
        char rule[256];
        unsigned name = mixed ? i % 137 : i;
        if (mixed && i % 4 == 0)
            append_css(sheet, i % 8 == 0 ? "@media screen {" : "@media print {");
        snprintf(rule, sizeof(rule), "%s.%c%u%s { %s }",
            mixed ? (i % 3 == 0 ? ".ancestor div" : i % 3 == 1 ? "span" : "") : "",
            mixed && i % 7 == 0 ? 'C' : 'c', name,
            mixed && i % 11 == 0 ? ", .c0, .c0" : "",
            mixed && i % 17 == 0 ? "" : "color: red;");
        append_css(sheet, rule);
        if (mixed && i % 4 == 0)
            append_css(sheet, "}");
    }
    if (mixed)
        append_css(sheet, "div { color: blue } #target { color: green } * { color: black }");
    assert(css_stylesheet_data_done(sheet) == (pending_import ? CSS_IMPORTS_PENDING : CSS_OK));
    return sheet;
}

static void check_size(css_selector_hash *hash)
{
    hash_t *tables[] = {&hash->elements, &hash->classes, &hash->ids};
    size_t expected = sizeof(*hash), actual;
    for (size_t t = 0; t < 3; t++) {
        hash_t *table = tables[t];
        expected += table->n_slots * sizeof(hash_entry);
        for (size_t i = 0; i < table->n_slots; i++) {
            for (hash_entry *e = table->slots[i].next; e != NULL; e = e->next)
                expected += sizeof(*e);
        }
    }
    for (hash_entry *e = hash->universal.next; e != NULL; e = e->next)
        expected += sizeof(*e);
    assert(test_hash_size(hash, &actual) == CSS_OK);
    assert(actual == expected);
}

static css_selector_hash *copy_hash(css_selector_hash *source)
{
    css_selector_hash *copy;
    hash_t *tables[] = {&source->elements, &source->classes, &source->ids};
    assert(test_hash_create(&copy) == CSS_OK);
    for (size_t t = 0; t < 3; t++) {
        for (size_t i = 0; i < tables[t]->n_slots; i++) {
            for (hash_entry *e = &tables[t]->slots[i]; e != NULL && e->sel != NULL; e = e->next)
                assert(test_hash_insert(copy, e->sel) == CSS_OK);
        }
    }
    for (hash_entry *e = &source->universal; e != NULL && e->sel != NULL; e = e->next)
        assert(test_hash_insert(copy, e->sel) == CSS_OK);
    return copy;
}

static void compare_lookups(css_selector_hash *before, css_selector_hash *after)
{
    css_select_strings strings;
    css_bloom bloom[CSS_BLOOM_SIZE];
    css_media media = {0};
    css_unit_ctx units = {0};
    struct css_hash_selection_requirments req = {
        .str = &strings, .media = &media, .unit_ctx = &units, .node_bloom = bloom,
    };
    assert(css_select_strings_intern(&strings) == CSS_OK);
    for (unsigned variant = 0; variant < 8; variant++) {
        memset(bloom, variant & 1 ? 0xff : 0, sizeof(bloom));
        media.type = variant & 2 ? CSS_MEDIA_SCREEN : CSS_MEDIA_PRINT;
        const char *element = variant & 4 ? "div" : "span";
        assert(lwc_intern_string(element, strlen(element), &req.qname.name) == lwc_error_ok);
        for (unsigned name = 0; name < 138; name++) {
            char text[32];
            snprintf(text, sizeof(text), "%c%u", variant & 1 ? 'C' : 'c', name);
            assert(lwc_intern_string(text, strlen(text), &req.class) == lwc_error_ok);
            css_selector_hash_iterator ai, bi;
            const css_selector **a, **b;
            assert(test_hash_find_by_class(before, &req, &ai, &a) == CSS_OK);
            assert(test_hash_find_by_class(after, &req, &bi, &b) == CSS_OK);
            size_t matched = 0;
            while (*a != NULL || *b != NULL) {
                if (*a != *b) {
                    fprintf(stderr, "Lookup order changed: class=%s variant=%u match=%zu\n", text, variant, matched);
                    abort();
                }
                matched++;
                assert(ai(&req, a, &a) == CSS_OK);
                assert(bi(&req, b, &b) == CSS_OK);
            }
            lwc_string_unref(req.class);
        }
        lwc_string_unref(req.qname.name);
    }
    css_select_strings_unref(&strings);
}

static void test_thresholds(void)
{
    const struct { unsigned selectors; size_t slots; } cases[] = {
        {0, 64}, {128, 64}, {129, 128}, {256, 128}, {257, 256}, {2048, 1024}, {2049, 1024},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        css_stylesheet *sheet = make_sheet(cases[i].selectors, false, i == 2);
        if (sheet->selectors->classes.n_slots != cases[i].slots) {
            fprintf(stderr, "Wrong table size for %u selectors\n", cases[i].selectors);
            abort();
        }
        assert(sheet->selectors->elements.n_slots == DEFAULT_SLOTS);
        assert(sheet->selectors->ids.n_slots == DEFAULT_SLOTS);
        check_size(sheet->selectors);
        assert(css_stylesheet_destroy(sheet) == CSS_OK);
    }
}

static void test_growth(void)
{
    css_stylesheet *sheet = make_sheet(1000, true, false);
    css_selector_hash *before = copy_hash(sheet->selectors);
    css_selector_hash *after = copy_hash(sheet->selectors);
    hash_entry *original = after->classes.slots;
    size_t original_size = after->hash_size;

    for (int allocation = 0; allocation < 2; allocation++) {
        fail_allocation = allocation;
        allocation_count = 0;
        test_hash_finish(after);
        fail_allocation = -1;
        assert(allocation_count == allocation + 1);
        assert(after->classes.slots == original && after->classes.n_slots == DEFAULT_SLOTS);
        assert(after->hash_size == original_size);
        check_size(after);
        compare_lookups(before, after);
    }

    test_hash_finish(after);
    assert(after->classes.n_slots > DEFAULT_SLOTS);
    assert(after->elements.n_slots == DEFAULT_SLOTS && after->ids.n_slots == DEFAULT_SLOTS);
    check_size(after);
    compare_lookups(before, after);
    compare_lookups(before, sheet->selectors); /* Real parser completion hook. */
    allocation_count = 0;
    test_hash_finish(after);
    assert(allocation_count == 0); /* Finishing twice is harmless. */

    /* Insertion and head removal must still preserve blooms and allocation
     * accounting after growth, including chains with equal-specificity ties. */
    for (size_t i = 0; i < after->classes.n_slots; i++) {
        hash_entry *head = &after->classes.slots[i];
        if (head->sel != NULL) {
            const css_selector *selector = head->sel;
            assert(test_hash_remove(after, selector) == CSS_OK);
            assert(test_hash_insert(after, selector) == CSS_OK);
        }
        while (head->sel != NULL) {
            css_bloom expected[CSS_BLOOM_SIZE];
            _chain_bloom_generate(head->sel, expected);
            assert(memcmp(expected, head->sel_chain_bloom, sizeof(expected)) == 0);
            assert(test_hash_remove(after, head->sel) == CSS_OK);
            check_size(after);
        }
    }
    assert(test_hash_destroy(after) == CSS_OK);
    assert(test_hash_destroy(before) == CSS_OK);
    assert(css_stylesheet_destroy(sheet) == CSS_OK);
}

int main(void)
{
    test_thresholds();
    test_growth();
    puts("PASS");
    return 0;
}
