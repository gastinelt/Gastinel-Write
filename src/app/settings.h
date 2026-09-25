// app/settings.h — reglages persistants de l application.
//
// FORMAT DE PERSISTANCE : meme famille que recent.txt — un fichier texte
// UTF-8, une entree par ligne, `cle=valeur`, fins de ligne CRLF. Les lignes
// vides et celles commencant par '#' sont ignorees, une cle inconnue est
// ignoree silencieusement.
//
// Pourquoi ne pas avoir etendu recent.txt : les deux fichiers n ont ni le meme
// rythme d ecriture (les recents changent a chaque ouverture de note, les
// reglages presque jamais) ni la meme consequence en cas de corruption. Les
// separer garde chacun trivialement reparable a la main. Le format, lui, est
// bien le meme mecanisme — texte a plat, pas de parseur — comme demande.
//
// EMPLACEMENT : %APPDATA%\Gastinel\Write\settings.txt, a cote de recent.txt.
//
// Cles reconnues :
//   dark_mode=0|1
//   keyboard_first=0|1
//   default_font_size=<entier, borne a [kMinFontSize, kMaxFontSize]>
//   font=<nom affiche>|<nom de fichier dans assets/fonts/custom>
//     (repetable ; l ordre du fichier est l ordre d affichage)
//
// La liste des polices est portee par ce fichier et non par le contenu du
// dossier custom/ : c est la liste qui fait foi. Un .ttf depose a la main dans
// le dossier n apparait donc pas, et une entree dont le fichier a disparu est
// eliminee au chargement. Ce choix evite d avoir a enumerer un repertoire — et
// surtout evite qu un fichier jamais valide par le parser se retrouve propose
// a l utilisateur.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "app/i18n.h"

namespace gw::app {

// Une police proposee a l utilisateur.
struct FontEntry {
    std::string display_name; // "Inter", "Fira Sans"...
    std::string file_name;    // vide pour la police integree
    bool built_in = false;    // Inter : toujours presente, non supprimable
};

class Settings {
public:
    // La police integree est presente des la construction, avant meme load() :
    // fonts() n est donc jamais vide, y compris si le fichier de reglages est
    // absent, illisible, ou si personne n a appele load(). Aucun appelant n a
    // a traiter le cas « liste vide ».
    Settings();

    static constexpr int kMinFontSize = 8;
    static constexpr int kMaxFontSize = 48;
    static constexpr int kDefaultFontSize = 17; // le corps d editeur historique

    // Charge les reglages et applique immediatement le mode sombre au theme.
    // `custom_fonts_dir` sert a verifier que les fichiers references existent
    // encore ; les entrees orphelines sont retirees et le fichier reecrit.
    void load(const std::string& custom_fonts_dir);

    bool dark_mode() const { return dark_mode_; }
    void set_dark_mode(bool dark);

    // Mode clavier-first : la barre de mise en forme disparait et la syntaxe
    // Markdown legere se rend en direct dans l editeur (cf.
    // doc/markdown_live.h). Ce n est PAS un format de document alternatif —
    // les deux modes produisent exactement le meme modele, donc la bascule est
    // immediate et ne perd aucune mise en forme deja posee.
    bool keyboard_first() const { return keyboard_first_; }
    void set_keyboard_first(bool on);

    // Reglages de demarrage
    bool open_last_note() const { return open_last_note_; }
    void set_open_last_note(bool on);

    bool show_quote_of_the_day() const { return show_quote_of_the_day_; }
    void set_show_quote_of_the_day(bool on);

    enum class QuoteDuration {
        WhileLoading = 0,
        FiveSeconds = 5,
        FifteenSeconds = 15,
    };

    QuoteDuration quote_duration() const { return quote_duration_; }
    void set_quote_duration(QuoteDuration duration);

    i18n::Language language() const { return language_; }
    void set_language(i18n::Language lang);

    bool smooth_caret() const { return smooth_caret_; }
    void set_smooth_caret(bool on);

    int default_font_size() const { return default_font_size_; }
    void set_default_font_size(int size);

    // Inter d abord, puis les polices importees dans l ordre d import.
    const std::vector<FontEntry>& fonts() const { return fonts_; }

    // Importe un .ttf : validation par le parser, copie dans
    // `custom_fonts_dir`, ajout a la liste. Retourne false et renseigne
    // `error` sans rien ecrire si le fichier n est pas exploitable.
    bool import_font(const std::string& source_path, const std::string& custom_fonts_dir,
                     std::string& error);

    // Retire une police importee. Sans effet sur la police integree.
    // Le fichier copie est supprime : le garder laisserait grossir un dossier
    // que plus rien ne reference.
    bool remove_font(std::size_t index, const std::string& custom_fonts_dir);

    static std::string storage_path();

private:
    void save() const;

    bool dark_mode_ = false;
    bool keyboard_first_ = false;
    bool open_last_note_ = false;
    bool show_quote_of_the_day_ = true;
    QuoteDuration quote_duration_ = QuoteDuration::FiveSeconds;
    i18n::Language language_ = i18n::Language::French;
    bool smooth_caret_ = true;
    int default_font_size_ = kDefaultFontSize;
    std::vector<FontEntry> fonts_;
};

} // namespace gw::app
