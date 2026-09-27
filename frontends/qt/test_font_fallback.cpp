/* SPDX-License-Identifier: GPL-2.0-only */
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#if QT_CONFIG(fontconfig)
#include <fontconfig/fontconfig.h>
#endif

extern "C" {
#include <libcss/properties.h>
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
}
#include "qt/capture_inspect.h"
#include "qt/layout.h"
#include "qt/misc.h"

static int repaints;
nserror nsqt_schedule(int, void (*)(void *), void *) { ++repaints; return NSERROR_OK; }
extern bool font_miss_occurred;

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::exit(EXIT_FAILURE); \
} } while (0)

struct FontList {
    std::vector<lwc_string *> names;
    plot_font_style_t style = {};

    FontList(std::initializer_list<const char *> families,
        plot_font_generic_family_t generic = PLOT_FONT_FAMILY_SANS_SERIF)
    {
        for (const char *name : families) {
            lwc_string *interned;
            REQUIRE(lwc_intern_string(name, std::strlen(name), &interned) == lwc_error_ok);
            names.push_back(interned);
        }
        names.push_back(nullptr);
        style.families = names.data();
        style.family = generic;
        style.size = 12 * PLOT_STYLE_SCALE;
        style.weight = 400;
    }

    ~FontList()
    {
        for (lwc_string *name : names)
            if (name != nullptr)
                lwc_string_unref(name);
    }

    void check(const char *label, const char *expected)
    {
        for (int repeat = 0; repeat < 2; ++repeat) {
            QFont font = nsqt_capture_font(&style);
            QString actual = QFontInfo(font).family();
            if (actual != QString::fromUtf8(expected)) {
                std::fprintf(stderr, "%s (repeat %d): expected %s, got %s (selected %s)\n",
                    label, repeat, expected, qPrintable(actual), qPrintable(font.family()));
                std::exit(EXIT_FAILURE);
            }
        }
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(argc == 2);
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    QString directory = QString::fromLocal8Bit(argv[1]);
    REQUIRE(QFontDatabase::addApplicationFont(directory + "/normal-400.ttf") >= 0);
    REQUIRE(QFontDatabase::addApplicationFont(directory + "/weight-500.ttf") >= 0);
    nsoption_set_charp(font_serif, strdup("Wisp Normal Test"));
    nsoption_set_charp(font_mono, strdup("Wisp Weight Test"));

    // Keep each list alive while its address is a key in the frontend font cache.
    FontList direct({"Wisp Normal Test", "Wisp Weight Test"});
    direct.check("first installed family wins", "Wisp Normal Test");
    FontList reversed({"Wisp Weight Test", "Wisp Normal Test"});
    reversed.check("CSS order is preserved", "Wisp Weight Test");
    FontList missing({"Wisp Absent Font 927", "Wisp Normal Test"});
    font_miss_occurred = false;
    missing.check("skip missing first family", "Wisp Normal Test");
    REQUIRE(font_miss_occurred);
    FontList lowercase({"wisp normal test"});
    lowercase.check("case-insensitive family", "Wisp Normal Test");
    FontList serif({"Wisp Absent Font 927"}, PLOT_FONT_FAMILY_SERIF);
    serif.check("configured serif fallback", "Wisp Normal Test");
    FontList mono({"Wisp Absent Font 927"}, PLOT_FONT_FAMILY_MONOSPACE);
    mono.check("configured monospace fallback", "Wisp Weight Test");
    FontList generic({}, PLOT_FONT_FAMILY_SERIF);
    generic.check("generic without named families", "Wisp Normal Test");

    QFont::insertSubstitutions("Wisp Registered Alias", {"Wisp Absent Font 927", "Wisp Weight Test"});
    FontList registered({"Wisp Registered Alias", "Wisp Normal Test"});
    registered.check("valid registered substitute before later CSS family", "Wisp Weight Test");
    QFont::insertSubstitution("Wisp Broken Alias", "Wisp Absent Font 927");
    FontList broken({"Wisp Broken Alias", "Wisp Normal Test"});
    broken.check("unavailable substitute must not end the search", "Wisp Normal Test");

#if QT_CONFIG(fontconfig)
    // Deterministic platform aliases, independent of the machine's fonts/config.
    REQUIRE(QFontDatabase::addApplicationFont(directory + "/foundry-regular.ttf") >= 0);
    REQUIRE(QFontDatabase::addApplicationFont(directory + "/foundry-italic.ttf") >= 0);
    const char config[] = "<fontconfig>"
        "<alias binding='strong'><family>Wisp Platform Alias</family>"
        "<prefer><family>Wisp Weight Test</family></prefer></alias>"
        "<alias binding='weak'><family>Wisp Platform Fallback</family>"
        "<prefer><family>Wisp Weight Test</family></prefer></alias>"
        "<alias binding='strong'><family>Wisp Foundry Alias</family>"
        "<prefer><family>Wisp Foundry Test</family></prefer></alias>"
        "</fontconfig>";
    REQUIRE(FcConfigParseAndLoadFromMemory(FcConfigGetCurrent(),
        reinterpret_cast<const FcChar8 *>(config), FcTrue));
    FontList platform({"Wisp Platform Alias", "Wisp Normal Test"});
    platform.check("strong platform alias", "Wisp Weight Test");
    FontList weak({"Wisp Platform Fallback", "Wisp Normal Test"});
    weak.check("weak platform fallback must not hide the next CSS family", "Wisp Normal Test");
    FontList foundries({"Wisp Foundry Alias", "Wisp Normal Test"});
    foundries.style.weight = 300;
    foundries.check("upright alias with multiple foundries", "Wisp Foundry Test [UKWN]");
    foundries.style.flags = FONTF_ITALIC;
    foundries.check("italic alias with multiple foundries", "Wisp Foundry Test [URW ]");
#endif

    FontList late({"Wisp Arriving Web Font", "Wisp Weight Test"});
    font_miss_occurred = false;
    late.check("before web font arrives", "Wisp Weight Test");
    REQUIRE(font_miss_occurred);
    QFile file(directory + "/normal-400.ttf");
    REQUIRE(file.open(QIODevice::ReadOnly));
    QByteArray data = file.readAll();
    char css_name[] = "Wisp Arriving Web Font";
    font_variant_id id = {css_name, CSS_FONT_WEIGHT_400, CSS_FONT_STYLE_NORMAL};
    REQUIRE(nsqt_layout_table->load_font_data(&id,
        reinterpret_cast<const uint8_t *>(data.constData()), data.size()) == NSERROR_OK);
    REQUIRE(repaints == 1);
    late.check("web font alias and cached fallback invalidation", "Wisp Normal Test");

    nsoption_finalise(nullptr, nullptr);
    std::puts("Qt font fallback: family order, missing names, generics, aliases and late web fonts passed.");
    return EXIT_SUCCESS;
}
