/* Unicode entry storage and HTML transport conversion, without a rendered tree. */
#include <check.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <libdom/bindings/hubbub/parser.h>
#include <wisp/content/fetch.h>
#include <wisp/content/handlers/html/form_internal.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/form_data.h"
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static dom_string *bytes(const char *text, size_t length)
{
    dom_string *string = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, length, &string), DOM_NO_ERR);
    return string;
}

static dom_string *string(const char *text)
{
    return bytes(text, strlen(text));
}

static void expect_string(dom_string *value, const char *expected, size_t length)
{
    ck_assert_uint_eq(dom_string_byte_length(value), length);
    ck_assert_int_eq(memcmp(dom_string_data(value), expected, length), 0);
}

static void expect_encoded(struct html_form_data *data, const char *encoding, const char *expected)
{
    char *encoded = NULL;
    ck_assert_int_eq(html_form_data_urlencode(data, encoding, &encoded), NSERROR_OK);
    ck_assert_str_eq(encoded, expected);
    free(encoded);
}

START_TEST(test_scalar_entries)
{
    const char value[] = "a\0b\r\n\xed\xa0\x80\xf0\x9f\x98\x80\xed\xbf\xbf";
    const char scalar[] = "a\0b\r\n\xef\xbf\xbd\xf0\x9f\x98\x80\xef\xbf\xbd";
    dom_string *input = bytes(value, sizeof(value) - 1);
    struct html_form_data data = {0};
    ck_assert_int_eq(html_form_data_append(&data, input, input), NSERROR_OK);
    expect_string(data.entries[0].name, scalar, sizeof(scalar) - 1);
    expect_string(data.entries[0].value, scalar, sizeof(scalar) - 1);
    expect_string(input, value, sizeof(value) - 1); /* Original DOMString unchanged. */
    dom_string_unref(input);
    expect_encoded(
        &data, "UTF-8", "a%00b%0D%0A%EF%BF%BD%F0%9F%98%80%EF%BF%BD=a%00b%0D%0A%EF%BF%BD%F0%9F%98%80%EF%BF%BD");
    html_form_data_fini(&data);
}
END_TEST

START_TEST(test_transport_newlines_and_duplicates)
{
    dom_string *name = string("line\nname"), *value = string("A\rB\nC\r\nD\r\rE** ~+-._é😀");
    struct html_form_data data = {0};
    ck_assert_int_eq(html_form_data_append(&data, name, value), NSERROR_OK);
    ck_assert_int_eq(html_form_data_append(&data, name, NULL), NSERROR_OK);
    expect_encoded(&data, "UTF-8",
        "line%0D%0Aname=A%0D%0AB%0D%0AC%0D%0AD%0D%0A%0D%0AE**+%7E%2B-._%C3%A9%F0%9F%98%80&line%0D%0Aname=");
    ck_assert_ptr_eq(data.entries[0].name, name); /* No copy needed for scalar UTF-8. */
    ck_assert_ptr_eq(data.entries[0].value, value);
    expect_string(value, "A\rB\nC\r\nD\r\rE** ~+-._é😀", strlen("A\rB\nC\r\nD\r\rE** ~+-._é😀"));
    dom_string_unref(name);
    dom_string_unref(value);
    html_form_data_fini(&data);
    expect_encoded(&data, "UTF-8", "");
    ck_assert_int_eq(html_form_data_append(&data, NULL, NULL), NSERROR_OK);
    expect_encoded(&data, "UTF-8", "=");
    html_form_data_fini(&data);
}
END_TEST

START_TEST(test_legacy_output_encoding)
{
    struct html_form_data data = {0};
    dom_string *name = string("q"), *value = bytes("é€\0😀", sizeof("é€\0😀") - 1);
    ck_assert_int_eq(html_form_data_append(&data, name, value), NSERROR_OK);
    expect_encoded(&data, "windows-1252", "q=%E9%80%00%26%23128512%3B");
    expect_encoded(&data, "UTF-8", "q=%C3%A9%E2%82%AC%00%F0%9F%98%80");
    char *encoded = (char *)1;
    ck_assert_int_eq(html_form_data_urlencode(&data, "not-an-encoding", &encoded), NSERROR_BAD_ENCODING);
    ck_assert_ptr_null(encoded);
    ck_assert_int_eq(html_form_data_urlencode(&data, "ASCII//TRANSLIT", &encoded), NSERROR_BAD_ENCODING);
    ck_assert_ptr_null(encoded);
    dom_string_unref(name);
    dom_string_unref(value);
    html_form_data_fini(&data);
}
END_TEST

START_TEST(test_stateful_encoding_boundaries)
{
    struct html_form_data data = {0};
    dom_string *value = string("あ😀あ");
    ck_assert_int_eq(html_form_data_append(&data, value, value), NSERROR_OK);
    expect_encoded(&data, "ISO-2022-JP",
        "%1B%24B%24%22%1B%28B%26%23128512%3B%1B%24B%24%22%1B%28B="
        "%1B%24B%24%22%1B%28B%26%23128512%3B%1B%24B%24%22%1B%28B");
    dom_string_unref(value);
    html_form_data_fini(&data);
}
END_TEST

START_TEST(test_multipart_lengths_and_files)
{
    struct html_form_data data = {0};
    dom_string *name = string("q"), *value = bytes("a\0b\nc", 5), *filename = string("news\nreport.txt");
    ck_assert_int_eq(html_form_data_append(&data, name, value), NSERROR_OK);
    char path[] = "/tmp/original.txt";
    ck_assert_int_eq(html_form_data_append_file(&data, name, filename, path), NSERROR_OK);
    path[5] = 'X';
    ck_assert_str_eq(data.entries[1].file_path, "/tmp/original.txt");
    ck_assert_int_eq(html_form_data_append_file(&data, name, NULL, NULL), NSERROR_OK);
    expect_encoded(&data, "UTF-8", "q=a%00b%0D%0Ac&q=news%0D%0Areport.txt&q=");
    struct fetch_multipart_data *parts = NULL;
    ck_assert_int_eq(html_form_data_multipart(&data, "UTF-8", &parts), NSERROR_OK);
    struct fetch_multipart_data *copy = fetch_multipart_data_clone(parts);
    ck_assert_ptr_nonnull(copy);
    fetch_multipart_data_destroy(parts);
    html_form_data_fini(&data);
    ck_assert_uint_eq(copy->value_length, 6);
    ck_assert_int_eq(memcmp(copy->value, "a\0b\r\nc", 6), 0);
    ck_assert(copy->next->file);
    ck_assert_str_eq(copy->next->value, "news\nreport.txt"); /* Multipart filenames keep original newlines. */
    ck_assert_str_eq(copy->next->rawfile, "/tmp/original.txt");
    ck_assert(copy->next->next->file);
    ck_assert_uint_eq(copy->next->next->value_length, 0);
    ck_assert_str_eq(copy->next->next->rawfile, "");
    fetch_multipart_data_destroy(copy);
    dom_string_unref(name);
    dom_string_unref(value);
    dom_string_unref(filename);
}
END_TEST

START_TEST(test_multipart_header_nul_is_not_truncated)
{
    struct html_form_data data = {0};
    dom_string *nul = bytes("x\0y", 3), *normal = string("first");
    ck_assert_int_eq(html_form_data_append(&data, normal, normal), NSERROR_OK);
    if (_i == 0)
        ck_assert_int_eq(html_form_data_append(&data, nul, normal), NSERROR_OK);
    else
        ck_assert_int_eq(html_form_data_append_file(&data, normal, nul, "/tmp/file"), NSERROR_OK);
    struct fetch_multipart_data *parts = (void *)1;
    ck_assert_int_eq(html_form_data_multipart(&data, "UTF-8", &parts), NSERROR_NOT_IMPLEMENTED);
    ck_assert_ptr_null(parts);
    ck_assert_uint_eq(data.length, 2);
    html_form_data_fini(&data);
    dom_string_unref(nul);
    dom_string_unref(normal);
}
END_TEST

static dom_document *parse(const char *source)
{
    dom_document *document = NULL;
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)source, strlen(source)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    return document;
}

static dom_node *find(dom_document *document, const char *id)
{
    dom_string *name = string(id);
    dom_node *node = NULL;
    ck_assert_int_eq(dom_document_get_element_by_id(document, name, &node), DOM_NO_ERR);
    ck_assert_ptr_nonnull(node);
    dom_string_unref(name);
    return node;
}

START_TEST(test_construct_retains_unicode_and_files)
{
    dom_document *document = parse(
        "<!doctype html><form id=f><input id=i name=q value=old>"
        "<input name=check type=checkbox checked><input name=empty type=checkbox checked value=''>"
        "<textarea name=q>a\nb</textarea><select name=opt><option selected value='choice'>text</select>"
        "<input id=file name=upload type=file><input name=unselected type=file value='forged.txt'>"
        "<button id=button name=go value=yes>Send</button></form>");
    dom_node *form = find(document, "f"), *input = find(document, "i"), *file = find(document, "file");
    dom_node *button = find(document, "button");
    dom_string *value = bytes("fresh\0é", sizeof("fresh\0é") - 1);
    ck_assert_int_eq(dom_html_input_element_set_value((dom_html_input_element *)input, value), DOM_NO_ERR);
    dom_string_unref(value);
    value = string(_i == 0 ? "/home/user/private/report.txt" : "C:\\private\\report.txt");
    ck_assert_int_eq(dom_html_input_element_set_value((dom_html_input_element *)file, value), DOM_NO_ERR);
    dom_string_unref(value);
    char *path = strdup("/private/report.txt"), *old = NULL;
    ck_assert_int_eq(
        dom_node_set_user_data(file, corestring_dom___ns_key_file_name_node_data, path, NULL, &old), DOM_NO_ERR);
    struct html_form_data data = {0};
    ck_assert_int_eq(html_form_data_construct((dom_html_form_element *)form, button, &data), NSERROR_OK);
    ck_assert_uint_eq(data.length, 8);
    ck_assert_int_eq(
        dom_node_set_user_data(file, corestring_dom___ns_key_file_name_node_data, NULL, NULL, &old), DOM_NO_ERR);
    free(old);
    dom_node_unref(button);
    dom_node_unref(input);
    dom_node_unref(file);
    dom_node_unref(form);
    dom_node_unref(document);
    expect_string(data.entries[3].value, "a\nb", 3);
    ck_assert_str_eq(data.entries[5].file_path, "/private/report.txt");
    expect_encoded(
        &data, "UTF-8", "q=fresh%00%C3%A9&check=on&empty=&q=a%0D%0Ab&opt=choice&upload=report.txt&unselected=&go=yes");
    html_form_data_fini(&data);
}
END_TEST

START_TEST(test_gadget_initialization_preserves_dom_values)
{
    const char *types[] = {"hidden", "text", "password"};
    dom_document *document = parse("<!doctype html><form id=f><input id=i name=q></form>");
    dom_node *input = find(document, "i");
    dom_string *type = string(types[_i]);
    ck_assert_int_eq(dom_element_set_attribute(input, corestring_dom_type, type), DOM_NO_ERR);
    dom_string_unref(type);
    dom_string *value = bytes("first\0tail", 10), *actual = NULL;
    ck_assert_int_eq(dom_element_set_attribute(input, corestring_dom_value, value), DOM_NO_ERR);
    struct form *forms = html_forms_get_forms((dom_html_document *)document);
    ck_assert_ptr_nonnull(forms);
    struct form_control *control = html_forms_get_control_for_node(forms, input);
    ck_assert_ptr_nonnull(control);
    ck_assert_int_eq(dom_html_input_element_get_value((dom_html_input_element *)input, &actual), DOM_NO_ERR);
    ck_assert(dom_string_isequal(actual, value));
    dom_string_unref(actual);
    dom_string_unref(value);
    /* DOM-driven updates must keep working after presentation construction,
     * and updating the cached native value must release its previous reference. */
    const char *edits[] = {"second\0tail", "third\0tail"};
    for (unsigned i = 0; i < 2; i++) {
        value = bytes(edits[i], i == 0 ? 11 : 10);
        ck_assert_int_eq(dom_element_set_attribute(input, corestring_dom_value, value), DOM_NO_ERR);
        form_gadget_sync_with_dom(control);
        ck_assert_int_eq(dom_html_input_element_get_value((dom_html_input_element *)input, &actual), DOM_NO_ERR);
        ck_assert(dom_string_isequal(actual, value));
        dom_string_unref(actual);
        dom_string_unref(value);
    }
    ck_assert_int_eq(dom_element_remove_attribute(input, corestring_dom_value), DOM_NO_ERR);
    form_gadget_sync_with_dom(control);
    ck_assert_str_eq(control->value, "");
    ck_assert_ptr_null(control->node_value);
    form_free(forms);
    dom_node_unref(input);
    dom_node_unref(document);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_append_allocation_failure)
{
    struct html_form_data data = {0};
    dom_string *name = string("name"), *scalar = string("existing"), *surrogate = string("\xed\xa0\x80");
    for (unsigned i = 0; i < 16; i++)
        ck_assert_int_eq(html_form_data_append(&data, name, scalar), NSERROR_OK);
    bool succeeded = false;
    for (unsigned count = 0; count < 100; count++) {
        if (_i == 0)
            malloc_limit(count);
        else
            realloc_limit(count);
        nserror result = html_form_data_append_file(&data, surrogate, surrogate, "/tmp/file");
        malloc_limit(UINT_MAX);
        realloc_limit(UINT_MAX);
        if (result == NSERROR_OK) {
            ck_assert_uint_eq(data.length, 17);
            succeeded = true;
            break;
        }
        ck_assert_int_eq(result, NSERROR_NOMEM);
        ck_assert_uint_eq(data.length, 16);
        for (size_t i = 0; i < data.length; i++)
            ck_assert_ptr_eq(data.entries[i].value, scalar);
    }
    ck_assert(succeeded);
    dom_string_unref(name);
    dom_string_unref(scalar);
    dom_string_unref(surrogate);
    html_form_data_fini(&data);
}
END_TEST

START_TEST(test_allocation_failure)
{
    struct html_form_data data = {0};
    dom_string *name = string("name"), *value = string("é\nfile");
    for (unsigned i = 0; i < 17; i++)
        ck_assert_int_eq(html_form_data_append_file(&data, name, value, "/tmp/file"), NSERROR_OK);
    bool succeeded = false, failed = false;
    for (unsigned count = 0; count < 400; count++) {
        char *encoded = NULL;
        struct fetch_multipart_data *parts = NULL;
        if (_i % 3 == 1)
            realloc_limit(count);
        else if (_i % 3 == 0)
            malloc_limit(count);
        else
            calloc_limit(count);
        nserror result = _i < 3 ? html_form_data_urlencode(&data, "UTF-8", &encoded)
                                : html_form_data_multipart(&data, "UTF-8", &parts);
        malloc_limit(UINT_MAX);
        realloc_limit(UINT_MAX);
        calloc_limit(UINT_MAX);
        if (result == NSERROR_OK) {
            free(encoded);
            fetch_multipart_data_destroy(parts);
            succeeded = true;
            break;
        }
        ck_assert_int_eq(result, NSERROR_NOMEM);
        ck_assert_ptr_null(encoded);
        ck_assert_ptr_null(parts);
        ck_assert_uint_eq(data.length, 17);
        failed = true;
    }
    ck_assert(succeeded);
    /* URL encoding uses realloc for its buffers, not malloc. */
    if (_i != 0 && _i != 2)
        ck_assert(failed);
    dom_string_unref(name);
    dom_string_unref(value);
    html_form_data_fini(&data);
}
END_TEST
#endif

int main(void)
{
    if (corestrings_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Unicode form entries");
    TCase *tc = tcase_create("Entries and encoding");
    tcase_add_test(tc, test_scalar_entries);
    tcase_add_test(tc, test_transport_newlines_and_duplicates);
    tcase_add_test(tc, test_legacy_output_encoding);
    tcase_add_test(tc, test_stateful_encoding_boundaries);
    tcase_add_test(tc, test_multipart_lengths_and_files);
    tcase_add_loop_test(tc, test_multipart_header_nul_is_not_truncated, 0, 2);
    tcase_add_loop_test(tc, test_construct_retains_unicode_and_files, 0, 2);
    tcase_add_loop_test(tc, test_gadget_initialization_preserves_dom_values, 0, 3);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_append_allocation_failure, 0, 2);
    tcase_add_loop_test(tc, test_allocation_failure, 0, 6);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
