#include "app/fonts.h"

#include "platform/log.h"
#include "platform/system.h"

#include <cmath>

namespace gw::app {
namespace {

// Largeur fixe de l atlas. Elle determine combien de glyphes tiennent sur une
// etagere ; un grand corps a besoin de plus de largeur pour ne pas empiler
// trop d etageres.
int atlas_width_for(float pixel_size) { return pixel_size > 64.0f ? 1024 : 512; }

// Hauteur maximale. Elle n est pas allouee d avance — l atlas y grandit par
// doublement — donc on peut se permettre une borne large : c est ce qui laisse
// la place aux polices importees, sans rien couter tant qu elles ne sont pas
// la.
constexpr int kAtlasMaxHeight = 4096;

// Pente de l italique synthetique : environ 12 degres, la pente des italiques
// latins courants. Au-dela, les jambages se couchent et le texte devient
// penible a lire a l ecran, ou l antialiasing manque pour rattraper.
constexpr float kSyntheticSlant = 0.21f;

// Epaississement du gras synthetique, en pixels. Un seul : a un corps de
// texte (15 a 20 px), deux pixels bouchent deja les contreformes du « e » et
// du « a ».
constexpr int kSyntheticEmbolden = 1;

// Nom d affichage de la police integree. C est le seul nom que FontSet
// reconnait sans passer par register_font().
constexpr char kBuiltInName[] = "Inter";

int size_key(float pixel_size) { return static_cast<int>(std::lround(pixel_size * 10.0f)); }

} // namespace

bool FontSet::load(const std::string& fonts_dir, std::string& error) {
    if (!regular_.load_from_file(fonts_dir + "/Inter-Regular.ttf", error)) {
        // Repli : verification dans le dossier des polices systeme (ex: C:\Windows\Fonts)
        const std::string sys_fonts = platform::system_fonts_directory();
        if (!sys_fonts.empty() && regular_.load_from_file(sys_fonts + "\\Inter-Regular.ttf", error)) {
            platform::log("Inter Regular charge depuis le dossier systeme : %s", sys_fonts.c_str());
            std::string semibold_error;
            has_semibold_ = semibold_.load_from_file(sys_fonts + "\\Inter-SemiBold.ttf", semibold_error);
            return true;
        }
        return false;
    }

    std::string semibold_error;
    has_semibold_ = semibold_.load_from_file(fonts_dir + "/Inter-SemiBold.ttf", semibold_error);
    if (!has_semibold_) {
        const std::string sys_fonts = platform::system_fonts_directory();
        if (!sys_fonts.empty()) {
            has_semibold_ = semibold_.load_from_file(sys_fonts + "\\Inter-SemiBold.ttf", semibold_error);
        }
    }
    if (!has_semibold_) {
        // Degradation volontaire : le SemiBold nest quun accent typographique,
        // son absence ne doit pas empecher le programme de demarrer. Le gras
        // demande par un document sera alors synthetise, comme pour une police
        // importee.
        platform::log("Inter SemiBold indisponible (%s) : repli sur Inter Regular",
                      semibold_error.c_str());
    }
    return true;
}

const font::TtfFont& FontSet::font(Weight w) const {
    if (w == Weight::SemiBold && has_semibold_) return semibold_;
    return regular_;
}

render::GlyphAtlas& FontSet::atlas(Weight w, float pixel_size) {
    // Le SemiBold absent partage latlas du Regular : sinon on rasteriserait
    // deux fois les memes glyphes.
    const Weight effective = (w == Weight::SemiBold && !has_semibold_) ? Weight::Regular : w;
    const std::pair<int, int> key{static_cast<int>(effective), size_key(pixel_size)};

    const auto it = atlases_.find(key);
    if (it != atlases_.end()) return *it->second;

    auto created = std::make_unique<render::GlyphAtlas>();
    created->reset(atlas_width_for(pixel_size), kAtlasMaxHeight, pixel_size);
    render::GlyphAtlas& ref = *created;
    atlases_.emplace(key, std::move(created));

    return ref;
}

// --- Polices importees ------------------------------------------------------

void FontSet::register_font(const std::string& display_name, const std::string& file_path) {
    if (display_name.empty() || display_name == kBuiltInName) return;

    const auto existing = imported_.find(display_name);
    if (existing != imported_.end() && existing->second->path == file_path) return;

    auto entry = std::make_unique<Imported>();
    entry->path = file_path;
    imported_[display_name] = std::move(entry);
}

void FontSet::clear_registered_fonts() {
    imported_.clear();
    // Les atlas de variantes referencent les polices qui viennent de
    // disparaitre : les garder rendrait des glyphes rasterises depuis une
    // police qui nexiste plus dans la liste. Les atlas de linterface
    // (atlases_) ne sont bases que sur Inter et survivent.
    face_atlases_.clear();
    cache_valid_ = false;
}

FontSet::Imported* FontSet::resolve_imported(const std::string& display_name) {
    const auto it = imported_.find(display_name);
    if (it == imported_.end()) return nullptr;

    Imported& entry = *it->second;
    if (!entry.attempted) {
        // Chargement a la premiere utilisation reelle, pas a la declaration :
        // ouvrir toutes les polices au demarrage ferait payer a chaque
        // lancement des fichiers que le document courant nutilise pas.
        entry.attempted = true;
        std::string error;
        entry.ok = entry.font.load_from_file(entry.path, error);
        if (!entry.ok) {
            platform::log_error("Police « %s » illisible (%s) : repli sur Inter",
                                display_name.c_str(), error.c_str());
        }
    }
    return entry.ok ? &entry : nullptr;
}

FontFace FontSet::face(const std::string& family, bool bold, bool italic, float pixel_size) {
    const int sk = size_key(pixel_size);
    if (cache_valid_ && cache_bold_ == bold && cache_italic_ == italic &&
        cache_size_key_ == sk && cache_family_ == family) {
        return cached_face_;
    }

    const font::TtfFont* chosen = nullptr;
    std::string key_family;

    if (!family.empty() && family != kBuiltInName) {
        if (Imported* imported = resolve_imported(family)) {
            chosen = &imported->font;
            key_family = family;
        }
        // Introuvable ou illisible : on tombe sur Inter, sans le dire au
        // modele. Le document garde le nom demande — le fichier peut revenir.
    }

    // Le gras nest reel que sur Inter, seule famille livree en deux fichiers.
    const bool real_bold = chosen == nullptr && bold && has_semibold_;
    if (chosen == nullptr) {
        chosen = real_bold ? &semibold_ : &regular_;
        key_family = kBuiltInName;
    }

    render::Synthesis synthesis;
    if (italic) synthesis.slant = kSyntheticSlant;
    if (bold && !real_bold) synthesis.embolden = kSyntheticEmbolden;

    const std::string key = key_family + "|" + (bold ? "B" : "-") + (italic ? "I" : "-") + "|" +
                            std::to_string(sk);

    render::GlyphAtlas* ref = nullptr;
    const auto it = face_atlases_.find(key);
    if (it != face_atlases_.end()) {
        ref = it->second.get();
    } else {
        auto created = std::make_unique<render::GlyphAtlas>();
        created->reset(atlas_width_for(pixel_size), kAtlasMaxHeight, pixel_size, synthesis);
        ref = created.get();
        face_atlases_.emplace(key, std::move(created));
    }

    cached_face_ = FontFace{chosen, ref};
    cache_family_ = family;
    cache_bold_ = bold;
    cache_italic_ = italic;
    cache_size_key_ = sk;
    cache_valid_ = true;

    return cached_face_;
}

} // namespace gw::app
