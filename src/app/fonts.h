// app/fonts.h — jeu de polices et cache datlas.
//
// Le rasterizer travaille a taille fixe : un atlas ne vaut que pour un triplet
// (fichier de police, synthese appliquee, corps). Plutot que de multiplier les
// membres dans App, ce cache cree les atlas a la demande et les garde.
//
// Ils ne sont jamais liberes : le nombre de combinaisons reellement utilisees
// par un document est petit (une police, quatre graisses, trois ou quatre
// corps), et un atlas de 512 px de cote coute 256 Kio. Une eviction serait de
// la complexite sans contrepartie.
//
// --- GRAS ET ITALIQUE : CE QUI EST REEL, CE QUI EST SYNTHETISE --------------
//
// Nous navons que deux fichiers Inter : Regular et SemiBold. Il ny a pas
// ditalique, et une police importee par lutilisateur narrive quen un seul
// fichier.
//
//   gras sur Inter          -> Inter SemiBold, un vrai dessin
//   gras ailleurs           -> synthese : le glyphe est dilate dun pixel
//   italique partout        -> synthese : le contour est cisaille avant
//                              rasterization
//
// La synthese est faite sur les CONTOURS ou sur la couverture, pas sur limage
// finale : elle passe donc par le meme rasterizer que tout le reste, et un
// glyphe synthetise se pose exactement comme un glyphe reel. Cest la
// difference entre un faux gras propre et un texte redessine deux fois avec un
// decalage, qui bave et double les jointures.
//
// Cest une degradation assumee et visible : un vrai italique dessine na pas
// la meme forme quun romain penche (le « a » italique change de squelette).
// Le jour ou des fichiers Inter Italic seront ajoutes a assets/fonts, il
// suffira de les charger ici et la synthese sarretera delle-meme pour Inter.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "font/ttf.h"
#include "render/glyph_atlas.h"

namespace gw::app {

enum class Weight {
    Regular,
    SemiBold,
};

// Une variante concrete : la police reelle a utiliser et latlas qui va avec.
// Les deux vont toujours ensemble — un atlas ne veut rien dire sans la police
// qui la rempli — donc ils sont rendus ensemble plutot que par deux appels que
// lappelant pourrait desaccorder.
struct FontFace {
    const font::TtfFont* font = nullptr;
    render::GlyphAtlas* atlas = nullptr;
};

class FontSet {
public:
    // Charge Inter Regular et Inter SemiBold depuis `fonts_dir`.
    // Le SemiBold est optionnel : sil manque, on retombe sur le Regular et on
    // le signale, plutot que dempecher le programme de demarrer.
    bool load(const std::string& fonts_dir, std::string& error);

    const font::TtfFont& font(Weight w) const;

    // Atlas pour ce couple graisse/corps, cree au premier appel.
    render::GlyphAtlas& atlas(Weight w, float pixel_size);

    // --- Polices importees ---
    //
    // Declare une police disponible sous son nom daffichage. Le fichier nest
    // PAS ouvert ici : il lest au premier glyphe reellement demande. Un
    // document qui nomme une police jamais affichee ne coute donc rien, et une
    // police devenue illisible ne fait echouer que son propre rendu.
    void register_font(const std::string& display_name, const std::string& file_path);
    void clear_registered_fonts();

    // Variante a utiliser pour une plage de texte.
    //
    // `family` est un nom daffichage ; vide, inconnu ou illisible retombe sur
    // Inter — un document ne doit jamais cesser de saffichier parce quune
    // police a disparu. Le nom, lui, reste dans le document (cf. doc/style.h).
    FontFace face(const std::string& family, bool bold, bool italic, float pixel_size);

private:
    // Police reelle derriere un nom daffichage, chargee a la demande.
    // `ok` a false signifie « deja tente, echoue » : on ne retente pas a chaque
    // frame ce qui vient dechouer.
    struct Imported {
        std::string path;
        font::TtfFont font;
        bool attempted = false;
        bool ok = false;
    };

    Imported* resolve_imported(const std::string& display_name);

    font::TtfFont regular_;
    font::TtfFont semibold_;
    bool has_semibold_ = false;

    std::map<std::string, std::unique_ptr<Imported>> imported_;

    // Cle : graisse et corps arrondi au dixieme de pixel. Deux demandes a
    // 14.0 et 14.04 partagent donc le meme atlas.
    std::map<std::pair<int, int>, std::unique_ptr<render::GlyphAtlas>> atlases_;

    // Atlas des variantes de face(). La cle porte le nom de la police, les
    // deux drapeaux et le corps ; une chaine plutot quun tuple parce quelle
    // est directement lisible dans un debogueur, et que le cout dune
    // comparaison de chaine est nul a cote dune rasterization.
    std::map<std::string, std::unique_ptr<render::GlyphAtlas>> face_atlases_;

    // Cache mono-entree ultra-rapide pour eviter toute allocation a la frappe.
    bool cache_valid_ = false;
    bool cache_bold_ = false;
    bool cache_italic_ = false;
    int cache_size_key_ = 0;
    std::string cache_family_;
    FontFace cached_face_{};
};

} // namespace gw::app
