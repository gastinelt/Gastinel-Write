#include "app/settings.h"

#include "app/theme.h"
#include "doc/file_bytes.h"
#include "font/ttf.h"
#include "platform/log.h"
#include "platform/system.h"

#include <algorithm>
#include <cstdlib>

namespace gw::app {
namespace {

constexpr char kBuiltInFontName[] = "Inter";

// Decoupe en lignes, BOM retire. Identique au chargement de recent.txt : le
// fichier est prevu pour etre editable a la main, donc un editeur Windows peut
// tres bien y deposer un BOM.
std::vector<std::string> split_lines(const std::string& data) {
    std::size_t start = 0;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        start = 3;
    }
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t i = start; i <= data.size(); ++i) {
        const char c = i < data.size() ? data[i] : '\n';
        if (c == '\n' || c == '\r') {
            if (!line.empty()) lines.push_back(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    return lines;
}

std::string file_name_only(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem_of(const std::string& file_name) {
    const std::size_t dot = file_name.find_last_of('.');
    return dot == std::string::npos ? file_name : file_name.substr(0, dot);
}

// Nom de fichier sur : on ne recopie pas le nom fourni par lutilisateur tel
// quel dans notre dossier. Un nom venu dailleurs peut contenir des
// separateurs (donc ecrire hors du dossier), des deux-points, ou viser un nom
// reserve DOS. Tout ce qui nest pas alphanumerique, tiret ou souligne devient
// un souligne.
std::string sanitize_file_name(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_';
        out.push_back(safe ? static_cast<char>(c) : '_');
    }
    if (out.empty()) out = "police";
    if (out.size() > 48) out.resize(48);
    return out;
}

} // namespace

Settings::Settings() { fonts_.push_back(FontEntry{kBuiltInFontName, std::string(), true}); }

std::string Settings::storage_path() {
    const std::string dir = platform::user_data_directory("Gastinel", "Write");
    if (dir.empty()) return {};
    return dir + "\\settings.txt";
}

void Settings::load(const std::string& custom_fonts_dir) {
    // Recharger remet la liste a son etat de construction : Inter seule.
    fonts_.clear();
    fonts_.push_back(FontEntry{kBuiltInFontName, std::string(), true});
    keyboard_first_ = false;
    open_last_note_ = false;
    show_quote_of_the_day_ = true;
    quote_duration_ = QuoteDuration::FiveSeconds;

    const std::string path = storage_path();
    std::string data, error;
    if (path.empty() || !doc::read_file_binary(path, data, error)) {
        // Premier lancement : pas de fichier. Les defauts sont deja en place.
        app::set_dark_mode(dark_mode_);
        return;
    }

    bool dropped_orphan = false;
    for (const std::string& line : split_lines(data)) {
        if (line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "dark_mode") {
            dark_mode_ = !value.empty() && value[0] != '0';
        } else if (key == "keyboard_first") {
            keyboard_first_ = !value.empty() && value[0] != '0';
        } else if (key == "open_last_note") {
            open_last_note_ = !value.empty() && value[0] != '0';
        } else if (key == "quote_of_the_day") {
            show_quote_of_the_day_ = !value.empty() && value[0] != '0';
        } else if (key == "quote_duration") {
            if (value == "loading" || value == "0") {
                quote_duration_ = QuoteDuration::WhileLoading;
            } else if (value == "15s" || value == "15") {
                quote_duration_ = QuoteDuration::FifteenSeconds;
            } else {
                quote_duration_ = QuoteDuration::FiveSeconds;
            }
        } else if (key == "language") {
            language_ = i18n::language_from_code(value);
        } else if (key == "smooth_caret") {
            smooth_caret_ = !value.empty() && value[0] != '0';
        } else if (key == "default_font_size") {
            const int parsed = std::atoi(value.c_str());
            if (parsed > 0) default_font_size_ = std::clamp(parsed, kMinFontSize, kMaxFontSize);
        } else if (key == "font") {
            const std::size_t bar = value.find('|');
            if (bar == std::string::npos) continue;
            FontEntry entry;
            entry.display_name = value.substr(0, bar);
            entry.file_name = value.substr(bar + 1);
            if (entry.display_name.empty() || entry.file_name.empty()) continue;
            // Une entree dont le fichier a disparu ne doit pas rester
            // proposee : elle produirait un echec au premier usage.
            if (!platform::file_exists(custom_fonts_dir + "\\" + entry.file_name)) {
                platform::log("Parametres : police \"%s\" ignoree, fichier %s absent",
                              entry.display_name.c_str(), entry.file_name.c_str());
                dropped_orphan = true;
                continue;
            }
            fonts_.push_back(entry);
        }
    }

    app::set_dark_mode(dark_mode_);
    i18n::set_language(language_);
    if (dropped_orphan) save(); // le fichier reflete de nouveau la realite
}

void Settings::save() const {
    const std::string path = storage_path();
    if (path.empty()) return;

    std::string data = "# Gastinel Write - reglages. Editable a la main, une cle par ligne.\r\n";
    data += std::string("dark_mode=") + (dark_mode_ ? "1" : "0") + "\r\n";
    data += std::string("keyboard_first=") + (keyboard_first_ ? "1" : "0") + "\r\n";
    data += std::string("open_last_note=") + (open_last_note_ ? "1" : "0") + "\r\n";
    data += std::string("quote_of_the_day=") + (show_quote_of_the_day_ ? "1" : "0") + "\r\n";
    data += std::string("quote_duration=") +
            (quote_duration_ == QuoteDuration::WhileLoading ? "loading" :
             (quote_duration_ == QuoteDuration::FifteenSeconds ? "15s" : "5s")) + "\r\n";
    data += std::string("language=") + i18n::language_code(language_) + "\r\n";
    data += std::string("smooth_caret=") + (smooth_caret_ ? "1" : "0") + "\r\n";
    data += "default_font_size=" + std::to_string(default_font_size_) + "\r\n";
    for (const FontEntry& f : fonts_) {
        if (f.built_in) continue; // Inter est implicite, jamais ecrite
        data += "font=" + f.display_name + "|" + f.file_name + "\r\n";
    }

    std::string error;
    if (!doc::write_file_binary(path, data, error)) {
        platform::log_error("Parametres : enregistrement impossible (%s)", error.c_str());
    }
}

void Settings::set_dark_mode(bool dark) {
    if (dark == dark_mode_) return;
    dark_mode_ = dark;
    app::set_dark_mode(dark);
    save();
}

void Settings::set_keyboard_first(bool on) {
    if (on == keyboard_first_) return;
    keyboard_first_ = on;
    save();
}

void Settings::set_open_last_note(bool on) {
    if (on == open_last_note_) return;
    open_last_note_ = on;
    save();
}

void Settings::set_show_quote_of_the_day(bool on) {
    if (on == show_quote_of_the_day_) return;
    show_quote_of_the_day_ = on;
    save();
}

void Settings::set_quote_duration(QuoteDuration duration) {
    if (duration == quote_duration_) return;
    quote_duration_ = duration;
    save();
}

void Settings::set_language(i18n::Language lang) {
    if (lang == language_) return;
    language_ = lang;
    i18n::set_language(lang);
    save();
}

void Settings::set_smooth_caret(bool on) {
    if (on == smooth_caret_) return;
    smooth_caret_ = on;
    save();
}

void Settings::set_default_font_size(int size) {
    const int clamped = std::clamp(size, kMinFontSize, kMaxFontSize);
    if (clamped == default_font_size_) return;
    default_font_size_ = clamped;
    save();
}

bool Settings::import_font(const std::string& source_path, const std::string& custom_fonts_dir,
                           std::string& error) {
    // 1. Validation AVANT toute ecriture. On passe le fichier au parser reel,
    //    pas a un controle dextension : cest le seul test qui dise si la
    //    police sera effectivement affichable par notre pipeline.
    font::TtfFont probe;
    if (!probe.load_from_file(source_path, error)) return false;

    // 2. Verification quau moins un glyphe latin de base sort du parser. Une
    //    police peut passer la validation structurelle sans avoir de cmap
    //    exploitable pour notre usage (police dicones a plage privee,
    //    sous-ensemble ne couvrant pas lalphabet). Laccepter donnerait une
    //    interface vide, sans message.
    int usable = 0;
    const std::uint32_t probes[] = {0x41u, 0x61u, 0x65u, 0x30u}; // A a e 0
    for (std::uint32_t cp : probes) {
        const std::uint16_t gid = probe.glyph_index_for_codepoint(cp);
        if (gid == 0) continue;
        font::Glyph glyph;
        if (probe.load_glyph(gid, glyph)) ++usable;
    }
    if (usable == 0) {
        error = "aucun caractere latin de base lisible dans cette police";
        return false;
    }

    // 3. Copie. Le nom de fichier est assaini et desambigue : deux imports du
    //    meme nom ne doivent pas secraser lun lautre.
    if (!platform::create_directories(custom_fonts_dir)) {
        error = "impossible de creer " + custom_fonts_dir;
        return false;
    }

    const std::string original = file_name_only(source_path);
    const std::string display = stem_of(original);
    const std::string base = sanitize_file_name(display);

    std::string file_name = base + ".ttf";
    for (int suffix = 2; platform::file_exists(custom_fonts_dir + "\\" + file_name); ++suffix) {
        if (suffix > 99) {
            error = "trop de polices de meme nom deja importees";
            return false;
        }
        file_name = base + "_" + std::to_string(suffix) + ".ttf";
    }

    std::string bytes;
    if (!doc::read_file_binary(source_path, bytes, error)) return false;
    if (!doc::write_file_binary(custom_fonts_dir + "\\" + file_name, bytes, error)) return false;

    FontEntry entry;
    entry.display_name = display.empty() ? base : display;
    entry.file_name = file_name;
    fonts_.push_back(entry);
    save();

    platform::log("Police importee : \"%s\" -> custom/%s (%zu octets)", entry.display_name.c_str(),
                  file_name.c_str(), bytes.size());
    return true;
}

bool Settings::remove_font(std::size_t index, const std::string& custom_fonts_dir) {
    if (index >= fonts_.size() || fonts_[index].built_in) return false;

    const std::string file = custom_fonts_dir + "\\" + fonts_[index].file_name;
    fonts_.erase(fonts_.begin() + static_cast<std::ptrdiff_t>(index));
    save();
    // Un echec de suppression ne remet pas lentree en liste : le fichier
    // orphelin est sans consequence, une entree fantome ne lest pas.
    platform::delete_file(file);
    return true;
}

} // namespace gw::app
