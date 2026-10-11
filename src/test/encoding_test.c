/* Encoding Standard label matching and output-encoding rules. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utils/encoding.h"

struct label_case { const char *label, *name; };
static const struct label_case labels[] = {
    {" UTF8\r\n\t\f", "UTF-8"}, {"unicode11utf8", "UTF-8"},
    {"ASCII", "windows-1252"}, {"iso-8859-1", "windows-1252"}, {"latin1", "windows-1252"},
    {"iso88599", "windows-1254"}, {"ISO-8859-11", "windows-874"}, {"logical", "ISO-8859-8-I"},
    {"csiso88598e", "ISO-8859-8"}, {"l2", "ISO-8859-2"}, {"l3", "ISO-8859-3"},
    {"l4", "ISO-8859-4"}, {"cyrillic", "ISO-8859-5"}, {"asmo-708", "ISO-8859-6"},
    {"greek8", "ISO-8859-7"}, {"latin6", "ISO-8859-10"}, {"iso885913", "ISO-8859-13"},
    {"iso885914", "ISO-8859-14"}, {"l9", "ISO-8859-15"}, {"ISO-8859-16", "ISO-8859-16"},
    {"866", "IBM866"}, {"koi", "KOI8-R"}, {"koi8-ru", "KOI8-U"}, {"mac", "macintosh"},
    {"x-mac-ukrainian", "x-mac-cyrillic"}, {"x-cp1250", "windows-1250"},
    {"cp1251", "windows-1251"}, {"cp1253", "windows-1253"}, {"cp1255", "windows-1255"},
    {"cp1256", "windows-1256"}, {"cp1257", "windows-1257"}, {"cp1258", "windows-1258"},
    {"chinese", "GBK"}, {"gb18030", "gb18030"}, {"big5-hkscs", "Big5"},
    {"cseucpkdfmtjapanese", "EUC-JP"}, {"csiso2022jp", "ISO-2022-JP"},
    {"windows-31j", "Shift_JIS"}, {"windows-949", "EUC-KR"}, {"csiso2022kr", "replacement"},
    {"unicodefffe", "UTF-16BE"}, {"unicode", "UTF-16LE"}, {"x-user-defined", "x-user-defined"},
};

START_TEST(test_labels)
{
    const struct label_case *sample = &labels[_i];
    const char *name = wisp_encoding_from_label(sample->label, strlen(sample->label));
    ck_assert_msg(name != NULL && strcmp(name, sample->name) == 0,
        "label %s: expected %s, got %s", sample->label, sample->name, name == NULL ? "NULL" : name);
    const char *output = strcmp(name, "replacement") == 0 || strncmp(name, "UTF-16", 6) == 0 ? "UTF-8" : name;
    ck_assert_str_eq(wisp_encoding_for_output(name), output);
}
END_TEST

START_TEST(test_invalid_labels)
{
    const char *invalid[] = {"", " \r\n\t\f", "\vutf-8", "utf-8\v", "utf-8 suffix", "prefix-utf-8",
        "utf-8,windows-1252", "UTF-7", "UTF-32", "u.t.f.8", "ISO-8859-1//TRANSLIT", "utf\xc2\xa0-8",
        "this-is-longer-than-any-label-in-the-registry"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        ck_assert_msg(wisp_encoding_from_label(invalid[i], strlen(invalid[i])) == NULL,
            "invalid label was accepted: %s", invalid[i]);
    ck_assert_ptr_null(wisp_encoding_from_label("utf-8\0junk", sizeof("utf-8\0junk") - 1));
    ck_assert_str_eq(wisp_encoding_from_label("utf-8junk", 5), "UTF-8"); /* No terminator required. */
    ck_assert_ptr_null(wisp_encoding_from_label(NULL, 0));
    ck_assert_str_eq(wisp_encoding_for_output(NULL), "UTF-8");
}
END_TEST

START_TEST(test_native_single_byte_encoders)
{
    wisp_single_byte_encoder mac = wisp_encoding_get_single_byte_encoder("x-mac-cyrillic");
    wisp_single_byte_encoder user = wisp_encoding_get_single_byte_encoder("x-user-defined");
    ck_assert(mac != NULL && user != NULL);
    for (unsigned i = 0; i < 128; i++) {
        ck_assert_int_eq(mac(i), i);
        ck_assert_int_eq(user(i), i);
        ck_assert_int_eq(user(0xf780 + i), 0x80 + i);
    }
    /* Check every indexed mapping using the original standard data, not the
     * generated reverse table: this also exercises generation and sorting. */
    FILE *file = fopen(WISP_ENCODING_DATA_DIR "/index-x-mac-cyrillic.txt", "r");
    ck_assert_ptr_nonnull(file);
    char line[256];
    unsigned count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned pointer, codepoint;
        if (sscanf(line, "%u %x", &pointer, &codepoint) != 2)
            continue;
        ck_assert_msg(mac(codepoint) == pointer + 0x80,
            "Mac Cyrillic U+%04X: expected byte %02X, got %X", codepoint, pointer + 0x80, mac(codepoint));
        count++;
    }
    ck_assert(!ferror(file));
    fclose(file);
    ck_assert_uint_eq(count, 128);
    ck_assert_int_eq(mac(0x20ac), 0xff); /* Platform MACCYRILLIC uses a different mapping here. */
    ck_assert_int_eq(mac(0x00a4), -1);
    ck_assert_int_eq(user(0x20ac), -1);
    ck_assert_int_eq(user(0xf77f), -1);
    ck_assert_int_eq(user(0xf800), -1);
    const uint32_t invalid[] = {0x80, 0xd800, 0x1f600, 0x110000};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        ck_assert_int_eq(mac(invalid[i]), -1);
        ck_assert_int_eq(user(invalid[i]), -1);
    }
    ck_assert(wisp_encoding_get_single_byte_encoder("UTF-8") == NULL);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("Encoding labels");
    TCase *tc = tcase_create("Registry");
    tcase_add_loop_test(tc, test_labels, 0, sizeof(labels) / sizeof(labels[0]));
    tcase_add_test(tc, test_invalid_labels);
    tcase_add_test(tc, test_native_single_byte_encoders);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
