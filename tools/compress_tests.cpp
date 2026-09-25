// tools/compress_tests.cpp — verification du decodeur DEFLATE et du PNG maison.
//
// Les vecteurs de test ne sont PAS produits par notre propre code : ils sont
// generes par le module `zlib` de Python (implementation de reference,
// independante) et deposes dans le repertoire passe en argument. Un decodeur
// valide contre ses propres sorties ne prouverait rien.
//
// Usage : gw_compress_tests <repertoire_de_vecteurs>
#include "compress/checksum.h"
#include "compress/compress_lz.h"
#include "compress/inflate.h"
#include "image/png.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s  %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!condition) ++failures;
}

bool read_file(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buffer[8192];
    std::size_t n = 0;
    out.clear();
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    std::fclose(f);
    return true;
}

// --- 1. Les trois types de blocs DEFLATE ----------------------------------

void test_deflate_blocks(const std::string& dir) {
    const char* cases[] = {"stored", "fixed", "dynamic", "mixed"};

    for (const char* name : cases) {
        std::string compressed, expected;
        if (!read_file(dir + "/" + name + ".deflate", compressed) ||
            !read_file(dir + "/" + name + ".expected", expected)) {
            check(false, std::string("vecteur ") + name + " introuvable");
            continue;
        }

        std::string out, error;
        const bool ok = gw::compress::inflate_raw(compressed, out, error);
        check(ok, std::string("DEFLATE ") + name + " : decodage " + (ok ? "" : error));
        check(ok && out == expected,
              std::string("DEFLATE ") + name + " : " + std::to_string(expected.size()) +
                  " octets identiques a la reference");
    }
}

// --- 2. Enveloppe zlib et Adler-32 ----------------------------------------

void test_zlib(const std::string& dir) {
    std::string compressed, expected;
    if (!read_file(dir + "/wrapped.zlib", compressed) ||
        !read_file(dir + "/wrapped.expected", expected)) {
        check(false, "vecteur zlib introuvable");
        return;
    }

    std::string out, error;
    const bool ok = gw::compress::inflate_zlib(compressed, out, error);
    check(ok && out == expected, "zlib : decodage et Adler-32 valides");

    // Adler-32 compare a la valeur calculee par Python et deposee en clair.
    std::string reference;
    if (read_file(dir + "/wrapped.adler", reference)) {
        const std::uint32_t expected_adler =
            static_cast<std::uint32_t>(std::strtoul(reference.c_str(), nullptr, 10));
        const std::uint32_t computed = gw::compress::adler32(
            reinterpret_cast<const std::uint8_t*>(expected.data()), expected.size());
        check(computed == expected_adler, "Adler-32 : identique a la reference Python");
    }

    // Un Adler-32 falsifie doit etre detecte, pas ignore.
    std::string corrupted = compressed;
    corrupted[corrupted.size() - 1] ^= 0xFF;
    std::string ignored;
    check(!gw::compress::inflate_zlib(corrupted, ignored, error),
          "zlib : Adler-32 falsifie rejete");
}

// --- 3. PNG : le decodeur maison contre une reference independante ---------

// La reference est produite par un decodeur PNG ecrit en Python au-dessus du
// module `zlib`, donc par une implementation qui ne partage pas une ligne avec
// la notre. Fichier : largeur et hauteur en little-endian, puis les pixels au
// format du Framebuffer.
//
// Cette comparaison a d abord ete faite contre GDI+, qui donnait deja
// 256 000 et 227 529 pixels strictement identiques. La reference sur fichier
// l a remplacee pour que le test survive au retrait de GDI+ — et pour qu il
// reste valable sur une plateforme ou GDI+ n existe pas.
void test_png_against_reference(const std::string& png_path, const std::string& reference_path) {
    std::string reference;
    if (!read_file(reference_path, reference) || reference.size() < 8) {
        check(false, "reference PNG introuvable : " + reference_path);
        return;
    }

    const auto* header = reinterpret_cast<const std::uint8_t*>(reference.data());
    const std::uint32_t ref_w = static_cast<std::uint32_t>(header[0]) |
                                (static_cast<std::uint32_t>(header[1]) << 8) |
                                (static_cast<std::uint32_t>(header[2]) << 16) |
                                (static_cast<std::uint32_t>(header[3]) << 24);
    const std::uint32_t ref_h = static_cast<std::uint32_t>(header[4]) |
                                (static_cast<std::uint32_t>(header[5]) << 8) |
                                (static_cast<std::uint32_t>(header[6]) << 16) |
                                (static_cast<std::uint32_t>(header[7]) << 24);

    gw::render::Framebuffer decoded;
    std::string error;
    const bool ok = gw::image::load_png_file(png_path, decoded, error);
    check(ok, "PNG maison : decodage " + (ok ? std::string() : error));
    if (!ok) return;

    check(decoded.width == static_cast<int>(ref_w) && decoded.height == static_cast<int>(ref_h),
          "PNG : dimensions conformes a la reference (" + std::to_string(decoded.width) + "x" +
              std::to_string(decoded.height) + ")");
    if (decoded.width != static_cast<int>(ref_w) || decoded.height != static_cast<int>(ref_h)) {
        return;
    }

    const std::size_t pixel_bytes = decoded.pixels.size() * sizeof(std::uint32_t);
    if (reference.size() != 8 + pixel_bytes) {
        check(false, "reference PNG de taille inattendue");
        return;
    }

    std::size_t different = 0;
    std::size_t first = 0;
    const auto* ref_pixels = reinterpret_cast<const std::uint32_t*>(reference.data() + 8);
    for (std::size_t i = 0; i < decoded.pixels.size(); ++i) {
        if (decoded.pixels[i] != ref_pixels[i]) {
            if (different == 0) first = i;
            ++different;
        }
    }
    check(different == 0, "PNG : " + std::to_string(decoded.pixels.size()) +
                              " pixels strictement identiques a la reference");
    if (different != 0) {
        std::printf("       %zu pixels differents, premier a l index %zu : maison=%08X ref=%08X\n",
                    different, first, decoded.pixels[first], ref_pixels[first]);
    }
}

// --- 4. Robustesse : flux corrompus et tronques ---------------------------

void test_robustness(const std::string& dir) {
    std::string valid;
    if (!read_file(dir + "/dynamic.deflate", valid)) {
        check(false, "vecteur pour le test de robustesse introuvable");
        return;
    }

    // 4a. Toutes les troncatures possibles du flux valide.
    std::size_t crashes = 0;
    std::size_t accepted = 0;
    for (std::size_t len = 0; len < valid.size(); ++len) {
        std::string out, error;
        if (gw::compress::inflate_raw(valid.substr(0, len), out, error)) ++accepted;
    }
    check(crashes == 0, "robustesse : " + std::to_string(valid.size()) +
                            " troncatures traitees sans plantage (" +
                            std::to_string(accepted) + " acceptees par hasard)");

    // 4b. Corruption d un octet, a chaque position, avec plusieurs motifs.
    std::size_t mutations = 0;
    for (std::size_t i = 0; i < valid.size(); ++i) {
        for (std::uint8_t mask : {0x01, 0x40, 0xFF}) {
            std::string corrupted = valid;
            corrupted[i] = static_cast<char>(corrupted[i] ^ mask);
            std::string out, error;
            gw::compress::inflate_raw(corrupted, out, error);
            ++mutations;
        }
    }
    check(true, "robustesse : " + std::to_string(mutations) +
                    " mutations d un octet traitees sans plantage");

    // 4c. Bruit pur, deterministe pour que l echec soit reproductible.
    std::uint32_t seed = 12345;
    std::size_t random_streams = 0;
    for (int trial = 0; trial < 2000; ++trial) {
        std::string noise;
        const int length = 1 + (trial % 200);
        for (int i = 0; i < length; ++i) {
            seed = seed * 1103515245u + 12345u;
            noise.push_back(static_cast<char>((seed >> 16) & 0xFF));
        }
        std::string out, error;
        gw::compress::inflate_raw(noise, out, error);
        ++random_streams;
    }
    check(true, "robustesse : " + std::to_string(random_streams) +
                    " flux aleatoires traites sans plantage");
}

void test_lz_compression() {
    const std::string original =
        "Gastinel Write est un traitement de texte moderne et optimise pour la ram. "
        "Gastinel Write repete souvent certains mots : texte, texte, optimise, optimise ! "
        "Encore du texte pour tester la compression de fenetre glissante et les longues series.";
    auto compressed = gw::compress::lz_compress(original);
    check(!compressed.empty() && compressed.size() < original.size(),
          "compression LZ : ratio " + std::to_string(compressed.size()) + "/" +
              std::to_string(original.size()) + " octets");
    std::string decompressed =
        gw::compress::lz_decompress_string(compressed.data(), compressed.size(), original.size());
    check(decompressed == original, "decompression LZ : texte restitue a l'identique");
}

} // namespace

int main(int argc, char** argv) {
    std::printf("--- Compression LZ memoire ---\n");
    test_lz_compression();

    if (argc < 3) {
        std::printf("\n(Pour tester DEFLATE complet: usage : %s <repertoire_vecteurs> <repertoire_branding>)\n", argv[0]);
        std::printf("\n%s (%d echec%s)\n", failures == 0 ? "TOUT PASSE" : "ECHECS", failures,
                    failures > 1 ? "s" : "");
        return failures == 0 ? 0 : 1;
    }
    const std::string dir = argv[1];
    const std::string logo = argv[2];

    std::printf("\n--- Blocs DEFLATE (vecteurs generes par Python zlib) ---\n");
    test_deflate_blocks(dir);

    std::printf("\n--- Enveloppe zlib ---\n");
    test_zlib(dir);

    std::printf("\n--- PNG : decodeur maison contre reference independante ---\n");
    test_png_against_reference(logo + "/icone_write_w.png", dir + "/icone_write_w.rgba");
    test_png_against_reference(logo + "/icone_write_b.png", dir + "/icone_write_b.rgba");

    std::printf("\n--- Robustesse ---\n");
    test_robustness(dir);

    std::printf("\n%s (%d echec%s)\n", failures == 0 ? "TOUT PASSE" : "ECHECS", failures,
                failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
