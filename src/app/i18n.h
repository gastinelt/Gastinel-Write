#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gw::app::i18n {

enum class Language {
    French = 0,
    English,
    Spanish,
    German,
    Italian,
    Portuguese,
    Dutch,
    Polish,
    Russian,
    Turkish,
    Count
};

enum class StringId {
    MenuFile = 0,
    MenuEdit,
    MenuView,
    ActionNew,
    ActionOpen,
    ActionSave,
    ActionSaveAs,
    ActionHome,
    ActionUndo,
    ActionRedo,
    ActionCut,
    ActionCopy,
    ActionPaste,
    ActionSelectAll,
    ActionToggleDark,
    ActionToggleKeyboard,
    ActionInsertTable,
    ActionDeleteTable,

    HomeTagline,
    HomeNewNote,
    HomeOpenNote,
    HomeSettings,
    HomeRecent,
    HomeNoRecent,
    HomeUntitled,

    SettingsTitle,
    SettingsAppearance,
    SettingsDarkMode,
    SettingsFontSize,
    SettingsFonts,
    SettingsImportFont,
    SettingsStartup,
    SettingsOpenLast,
    SettingsShowQuote,
    SettingsQuoteDuration,
    SettingsQuoteDurLoading,
    SettingsQuoteDur5s,
    SettingsQuoteDur15s,
    SettingsLanguage,
    SettingsSmoothCaret,

    StatusBackend,
    StatusShortcuts,
    ConfirmDiscardTitle,
    ConfirmDiscardMessage,
    UntitledDoc,
    AppTitle,

    Count
};

// Accès direct a la chaîne traduite pour la langue courante
const char* tr(StringId id);
// Accès a la chaîne pour une langue spécifique
const char* tr(StringId id, Language lang);

// Gestion de la langue active
Language current_language();
void set_language(Language lang);

// Metadonnees de langue
const char* language_code(Language lang);
const char* language_name(Language lang);
Language language_from_code(std::string_view code);

struct LanguageEntry {
    Language lang;
    const char* code;
    const char* name;
};

const std::vector<LanguageEntry>& all_languages();

} // namespace gw::app::i18n
