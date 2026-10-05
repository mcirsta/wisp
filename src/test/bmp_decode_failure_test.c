/* Decoder contracts for incomplete BMP/ICO data and independently usable icons. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <libnsbmp.h>

struct source {
    uint8_t data[4096];
    size_t size;
};
static unsigned buffer_calls, fail_buffer_call;

static void *create_bitmap(int width, int height, unsigned flags)
{
    ck_assert(flags & BMP_CLEAR_MEMORY);
    return calloc((size_t)width * height, 4);
}

static uint8_t *get_buffer(void *bitmap)
{
    return ++buffer_calls == fail_buffer_call ? NULL : bitmap;
}

static bmp_bitmap_callback_vt callbacks = {create_bitmap, free, get_buffer};

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = value >> (8 * i);
}

static struct source dib(unsigned width, unsigned bpp, unsigned rows, unsigned masks)
{
    struct source s = {0};
    put32(s.data, 40);
    put32(s.data + 4, width);
    put32(s.data + 8, 4); /* XOR and AND bitmaps, each two rows high. */
    s.data[12] = 1;
    s.data[14] = bpp;
    s.size = 40;
    if (bpp < 16) {
        put32(s.data + 32, 2);
        s.data[s.size + 2] = 255; /* Palette zero: red. */
        s.data[s.size + 5] = 255; /* Palette one: green. */
        s.size += 8;
    }
    unsigned stride = ((width * bpp + 31) / 32) * 4;
    for (unsigned y = 0; y < rows; y++) {
        for (unsigned x = 0; x < width; x++) {
            if (bpp == 16) {
                s.data[s.size + x * 2 + 1] = 0x7c; /* RGB555 red. */
            } else if (bpp >= 24) {
                s.data[s.size + x * (bpp / 8) + 2] = 255;
                if (bpp == 32)
                    s.data[s.size + x * 4 + 3] = 255;
            }
        }
        s.size += stride;
    }
    s.size += masks * ((width + 31) / 32) * 4;
    return s;
}

static struct source collection(const struct source *images, unsigned count)
{
    struct source s = {0};
    s.data[2] = 1;
    s.data[4] = count;
    s.size = 6 + 16 * count;
    for (unsigned i = 0; i < count; i++) {
        uint8_t *entry = s.data + 6 + i * 16;
        entry[0] = images[i].data[4];
        entry[1] = 2;
        entry[4] = 1;
        entry[6] = images[i].data[14];
        put32(entry + 8, images[i].size);
        put32(entry + 12, s.size);
        memcpy(s.data + s.size, images[i].data, images[i].size);
        s.size += images[i].size;
    }
    return s;
}

static uint8_t *load_icon(ico_collection *ico, const struct source *source)
{
    /* Exact allocation lets ASan catch reads beyond truncated masks/data. */
    uint8_t *data = malloc(source->size);
    memcpy(data, source->data, source->size);
    buffer_calls = fail_buffer_call = 0;
    ck_assert_int_eq(ico_collection_create(ico, &callbacks), BMP_OK);
    ck_assert_int_eq(ico_analyse(ico, source->size, data), BMP_OK);
    return data;
}

START_TEST(test_partial_pixels)
{
    const unsigned depths[] = {1, 4, 8, 16, 24, 32};
    unsigned depth = depths[_i / 3], rows = _i % 3;
    struct source image = dib(8, depth, rows, rows == 2 && depth != 32 ? 2 : 0);
    struct source source = collection(&image, 1);
    ico_collection ico;
    uint8_t *data = load_icon(&ico, &source);
    bmp_image *bmp = ico_find(&ico, 8, 2);
    ck_assert_ptr_nonnull(bmp);
    bmp_result result = bmp_decode(bmp);
    ck_assert_msg(result == (rows == 2 ? BMP_OK : BMP_INSUFFICIENT_DATA),
        "depth=%u rows=%u result=%d", depth, rows, result);
    ck_assert_msg(bmp->decoded == (rows != 0), "depth=%u rows=%u decoded=%d", depth, rows, bmp->decoded);
    ck_assert_int_eq(bmp->decode_result, result);
    if (rows) {
        const uint8_t *pixels = bmp->bitmap;
        unsigned bottom = 8 * 4;
        ck_assert_uint_ge(pixels[bottom], 248);
        ck_assert_uint_eq(pixels[bottom + 2], 0);
        ck_assert_uint_eq(pixels[bottom + 3], 255);
        if (rows == 1)
            ck_assert_uint_eq(pixels[3], 0);
    } else {
        ck_assert_ptr_null(ico_find(&ico, 8, 2));
    }
    ico_finalise(&ico);
    free(data);
}
END_TEST

START_TEST(test_short_mask_and_retry)
{
    /* Width one still needs a mask byte. Missing masks are recoverable;
     * get_buffer failure during mask decoding remains retryable. */
    struct source image = dib(1, 24, 2, _i == 0 ? 0 : 2);
    if (_i == 3) {
        image.size -= 7; /* First mask byte survives; its padding and next row do not. */
        image.data[image.size - 1] = 0x80;
    }
    struct source source = collection(&image, 1);
    ico_collection ico;
    uint8_t *data = load_icon(&ico, &source);
    bmp_image *bmp = ico_find(&ico, 1, 2);
    if (_i == 1 || _i == 2)
        fail_buffer_call = _i; /* Failure at either colour or mask access. */
    bmp_result result = bmp_decode(bmp);
    if (_i == 0 || _i == 3) {
        ck_assert_int_eq(result, BMP_INSUFFICIENT_DATA);
        ck_assert(bmp->decoded);
        ck_assert_uint_eq(((uint8_t *)bmp->bitmap)[3], 255);
        if (_i == 3)
            ck_assert_uint_eq(((uint8_t *)bmp->bitmap)[7], 0);
    } else {
        ck_assert_int_eq(result, BMP_INSUFFICIENT_MEMORY);
        ck_assert(!bmp->decoded);
        ck_assert_ptr_eq(ico_find(&ico, 1, 2), bmp);
        fail_buffer_call = 0;
        ck_assert_int_eq(bmp_decode(bmp), BMP_OK);
        ck_assert(bmp->decoded);
        ck_assert_uint_eq(((uint8_t *)bmp->bitmap)[3], 255);
    }
    ico_finalise(&ico);
    free(data);
}
END_TEST

START_TEST(test_invalid_palette_index)
{
    struct source image = dib(8, 8, 2, 2);
    image.data[48 + _i] = 2; /* Only indices zero and one are defined. */
    struct source source = collection(&image, 1);
    ico_collection ico;
    uint8_t *data = load_icon(&ico, &source);
    bmp_image *bmp = ico_find(&ico, 8, 2);
    ck_assert_int_eq(bmp_decode(bmp), BMP_DATA_ERROR);
    ck_assert_int_eq(bmp->decoded, _i != 0);
    if (_i != 0)
        ck_assert_uint_eq(((uint8_t *)bmp->bitmap)[8 * 4 + 3], 255);
    ico_finalise(&ico);
    free(data);
}
END_TEST

START_TEST(test_alternate_entry)
{
    struct source images[] = {dib(16, 32, 0, 0), dib(32, 32, _i == 0 ? 2 : 0, 0)};
    struct source source = collection(images, 2);
    ico_collection ico;
    uint8_t *data = load_icon(&ico, &source);
    bmp_image *bad = ico_find(&ico, 16, 2);
    ck_assert_uint_eq(bad->width, 16);
    ck_assert_int_eq(bmp_decode(bad), BMP_INSUFFICIENT_DATA);
    bmp_image *other = ico_find(&ico, 16, 2);
    ck_assert_ptr_nonnull(other);
    ck_assert_uint_eq(other->width, 32);
    ck_assert_int_eq(bmp_decode(other), _i == 0 ? BMP_OK : BMP_INSUFFICIENT_DATA);
    if (_i == 0) {
        ck_assert_ptr_eq(ico_find(&ico, UINT16_MAX, UINT16_MAX), other);
        ck_assert_ptr_eq(ico_find(&ico, 16, 2), other);
    } else {
        ck_assert_ptr_null(ico_find(&ico, 16, 2));
    }
    ico_finalise(&ico);
    free(data);
}
END_TEST

START_TEST(test_invalid_member_header)
{
    struct source images[] = {dib(16, 32, 2, 0), dib(1, 32, 2, 0)};
    if (_i == 0)
        images[0].data[0] = 11; /* Unsupported DIB header. */
    else if (_i == 1)
        images[0].data[8] = 1; /* No complete XOR/AND height pair. */
    else if (_i == 2)
        images[0].size = 3; /* Incomplete header. */
    struct source source = collection(images, 2);
    if (_i == 3)
        put32(source.data + 18, UINT32_MAX); /* Invalid member offset. */
    ico_collection ico;
    uint8_t *data = load_icon(&ico, &source);
    bmp_image *bmp = ico_find(&ico, 16, 2);
    ck_assert_ptr_nonnull(bmp);
    ck_assert_uint_eq(bmp->width, 1);
    ck_assert_int_eq(bmp_decode(bmp), BMP_OK);
    ico_finalise(&ico);
    free(data);
}
END_TEST

START_TEST(test_rle_partial_pixels)
{
    unsigned bpp = _i < 3 ? 4 : 8;
    unsigned mode = _i % 3;
    struct source source = dib(mode == 2 && bpp == 4 ? 8 : 4, bpp, 0, 0);
    put32(source.data + 8, 2); /* Standalone BMP, not doubled ICO height. */
    put32(source.data + 16, bpp == 4 ? BMP_ENCODING_RLE4 : BMP_ENCODING_RLE8);
    size_t pixel_offset = source.size;
    if (mode == 1) {
        source.data[source.size++] = 3; /* A run of three valid red pixels. */
        source.data[source.size++] = 0;
    }
    if (mode < 2) {
        const uint8_t bad_delta[] = {0, 2, 99, 0};
        memcpy(source.data + source.size, bad_delta, sizeof(bad_delta));
        source.size += sizeof(bad_delta);
    } else {
        /* A literal run with a missing alignment byte. */
        source.data[source.size++] = 0;
        source.data[source.size++] = bpp == 4 ? 5 : 3;
        source.size += 3;
    }
    memmove(source.data + 14, source.data, source.size);
    memset(source.data, 0, 14);
    source.data[0] = 'B'; source.data[1] = 'M';
    source.size += 14;
    put32(source.data + 2, source.size);
    put32(source.data + 10, pixel_offset + 14);
    uint8_t *data = malloc(source.size);
    memcpy(data, source.data, source.size);
    bmp_image bmp;
    buffer_calls = fail_buffer_call = 0;
    bmp_create(&bmp, &callbacks);
    ck_assert_int_eq(bmp_analyse(&bmp, source.size, data), BMP_OK);
    bmp_result result = bmp_decode(&bmp);
    ck_assert_int_ne(result, BMP_OK);
    ck_assert_msg(bmp.decoded == (mode != 0), "bpp=%u mode=%u result=%d", bpp, mode, result);
    bmp_finalise(&bmp);
    free(data);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("BMP/ICO decoding failures");
    TCase *tc = tcase_create("Usable pixels and icon selection");
    tcase_add_loop_test(tc, test_partial_pixels, 0, 18);
    tcase_add_loop_test(tc, test_short_mask_and_retry, 0, 4);
    tcase_add_loop_test(tc, test_invalid_palette_index, 0, 2);
    tcase_add_loop_test(tc, test_alternate_entry, 0, 2);
    tcase_add_loop_test(tc, test_invalid_member_header, 0, 4);
    tcase_add_loop_test(tc, test_rle_partial_pixels, 0, 6);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
