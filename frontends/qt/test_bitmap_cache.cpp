/* SPDX-License-Identifier: GPL-2.0-only */
#include <QApplication>
#include <QPainter>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include <wisp/bitmap.h>
#include <wisp/plotters.h>
}
#include "qt/bitmap.h"
#include "qt/capture_inspect.h"
#include "qt/plotters.h"

// Thumbnail rendering is linked by bitmap.cpp but not used by these tests.
const struct plotter_table nsqt_plotters = {};

#define REQUIRE(expr)                                                                                                  \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                                                    \
            std::exit(EXIT_FAILURE);                                                                                   \
        }                                                                                                              \
    } while (0)

static qint64 stat(const char *name)
{
    return nsqt_capture_bitmap_cache_stats()[name].toInteger();
}

static void compare(QImage *image, int width, int height)
{
    REQUIRE(nsqt_bitmap_scaled(image, width, height) ==
        image->scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    auto *image = static_cast<QImage *>(nsqt_bitmap_table->create(64, 48, BITMAP_NONE));
    REQUIRE(image && !image->isNull());
    auto *pixels = reinterpret_cast<QRgb *>(nsqt_bitmap_table->get_buffer(image));
    for (int i = 0; i < 64 * 48; ++i)
        pixels[i] = qRgba(i % 255, (i * 3) % 255, (i * 7) % 255, i % 2 ? 128 : 255);
    nsqt_bitmap_table->modified(image);
    const qsizetype budget = stat("budget_bytes");
    compare(image, 32, 24);
    compare(image, 32, 24);
    REQUIRE(stat("hits") == 1 && stat("misses") == 1);
    QImage held = nsqt_bitmap_scaled(image, 32, 24);
    compare(image, 128, 96);
    REQUIRE(stat("entries") == 1);
    REQUIRE(held.size() == QSize(32, 24));
    compare(image, 64, 48); // Natural size bypasses scaling and the LRU.
    REQUIRE(stat("misses") == 2);

    // Decoders can retain a buffer and change it without asking Qt for bits again.
    qint64 key = image->cacheKey();
    for (int i = 0; i < 64 * 48; ++i)
        pixels[i] = qRgba(200, 0, 0, 255);
    REQUIRE(image->cacheKey() == key);
    nsqt_bitmap_table->modified(image);
    REQUIRE(stat("entries") == 0);
    compare(image, 32, 24);
    REQUIRE(nsqt_bitmap_scaled(image, 32, 24) != held);
    // Normal QImage mutation also changes the key even without a core notification.
    image->fill(Qt::blue);
    compare(image, 32, 24);
    REQUIRE(nsqt_bitmap_scaled(image, 32, 24).pixel(0, 0) == qRgb(0, 0, 255));
    nsqt_bitmap_table->get_buffer(image);
    REQUIRE(stat("entries") == 0);

    // A shared byte budget bounds multiple sources. Eviction cannot invalidate
    // an image that a painter already obtained by value.
    auto *other = static_cast<QImage *>(nsqt_bitmap_table->create(64, 48, BITMAP_OPAQUE));
    other->fill(Qt::green);
    nsqt_capture_bitmap_cache_limit(budget);
    held = nsqt_bitmap_scaled(image, 32, 24);
    qsizetype cost = stat("cost_bytes");
    nsqt_capture_bitmap_cache_limit(cost);
    compare(image, 32, 24);
    compare(other, 32, 24);
    REQUIRE(stat("entries") == 1 && stat("cost_bytes") <= cost);
    compare(image, 32, 24);
    REQUIRE(stat("misses") == 3);
    REQUIRE(held.pixel(0, 0) == qRgb(0, 0, 255));
    compare(image, 128, 96); // Too large to keep: still render accurately.
    REQUIRE(stat("entries") == 0);
    compare(image, 0, 10);
    REQUIRE(stat("entries") == 0);
    nsqt_capture_bitmap_cache_limit(0);
    compare(image, 32, 24);
    compare(image, 32, 24);
    REQUIRE(stat("entries") == 0 && stat("hits") == 0);
    nsqt_capture_bitmap_cache_limit(budget);
    compare(image, 32, 24);
    nsqt_bitmap_table->destroy(image);
    REQUIRE(stat("entries") == 0);
    nsqt_bitmap_table->destroy(other);
    std::puts("Qt bitmap cache: pixels, alpha, resize, mutation, ownership, eviction and destruction passed.");
    return EXIT_SUCCESS;
}
