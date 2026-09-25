#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app/i18n.h"
#include "app/settings.h"
#include "app/editor_view.h"
#include "doc/text_edit.h"

using namespace gw::app;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) \
    do { \
        if (expr) { \
            ++g_pass; \
        } else { \
            ++g_fail; \
            std::printf("FAIL: %s (line %d)\n", #expr, __LINE__); \
        } \
    } while (0)

void test_i18n_coverage() {
    std::printf("[TEST] i18n string coverage across 10 languages...\n");
    const auto& langs = i18n::all_languages();
    CHECK(langs.size() == 10);

    for (const auto& entry : langs) {
        CHECK(entry.name != nullptr && entry.name[0] != '\0');
        CHECK(entry.code != nullptr && entry.code[0] != '\0');

        // Test roundtrip from code
        const i18n::Language resolved = i18n::language_from_code(entry.code);
        CHECK(resolved == entry.lang);

        // Check each string in this language
        for (int s = 0; s < static_cast<int>(i18n::StringId::Count); ++s) {
            const char* str = i18n::tr(static_cast<i18n::StringId>(s), entry.lang);
            CHECK(str != nullptr);
            CHECK(str[0] != '\0');
        }
    }

    // Check language switching
    i18n::set_language(i18n::Language::Russian);
    CHECK(i18n::current_language() == i18n::Language::Russian);
    CHECK(std::string(i18n::tr(i18n::StringId::SettingsTitle)) == "Настройки");

    i18n::set_language(i18n::Language::Turkish);
    CHECK(i18n::current_language() == i18n::Language::Turkish);
    CHECK(std::string(i18n::tr(i18n::StringId::MenuFile)) == "Dosya");

    i18n::set_language(i18n::Language::Polish);
    CHECK(i18n::current_language() == i18n::Language::Polish);
    CHECK(std::string(i18n::tr(i18n::StringId::ActionCut)) == "Wytnij");

    i18n::set_language(i18n::Language::German);
    CHECK(i18n::current_language() == i18n::Language::German);
    CHECK(std::string(i18n::tr(i18n::StringId::SettingsDarkMode)) == "Dunkelmodus");

    i18n::set_language(i18n::Language::French); // restore
}

void test_settings_serialization() {
    std::printf("[TEST] Settings language and smooth_caret serialization...\n");
    Settings s;
    s.set_language(i18n::Language::Spanish);
    s.set_smooth_caret(false);

    CHECK(s.language() == i18n::Language::Spanish);
    CHECK(s.smooth_caret() == false);
    CHECK(i18n::current_language() == i18n::Language::Spanish);

    s.set_smooth_caret(true);
    CHECK(s.smooth_caret() == true);

    s.set_language(i18n::Language::French);
}

void test_smooth_caret_animation() {
    std::printf("[TEST] Smooth caret gliding and convergence physics...\n");
    EditorView view;
    CHECK(!view.is_animating());
    CHECK(view.anim_caret_x() < 0.0f);

    view.reset_caret_animation();
    CHECK(!view.is_animating());
    CHECK(view.anim_caret_x() < 0.0f);

    gw::doc::TextEdit edit;
    edit.insert_text("Gastinel Write - Pure C++ Word Processor");
    CHECK(!edit.document().paragraphs().empty());
    CHECK(!edit.document().paragraphs().front().text.empty());
}

int main() {
    test_i18n_coverage();
    test_settings_serialization();
    test_smooth_caret_animation();

    std::printf("Feature test summary: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
