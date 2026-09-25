#include "image/png.h"

#include "compress/checksum.h"
#include "compress/inflate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gw::image {
namespace {

constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

std::uint32_t read_u32(const std::uint8_t* p) {
    // PNG stocke ses entiers en gros-boutien, contrairement au zip.
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

// Predicteur de Paeth : choisit, parmi le voisin de gauche, celui du dessus et
// celui de la diagonale, celui dont la valeur est la plus proche de leur
// combinaison lineaire. C est le filtre le plus efficace du format, et le seul
// qui ne soit pas une simple soustraction.
std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = std::abs(p - static_cast<int>(a));
    const int pb = std::abs(p - static_cast<int>(b));
    const int pc = std::abs(p - static_cast<int>(c));
    if (pa <= pb && pa <= pc) return a;
    return (pb <= pc) ? b : c;
}

int channels_for_color_type(int color_type) {
    switch (color_type) {
        case 0: return 1; // gris
        case 2: return 3; // RVB
        case 3: return 1; // index de palette
        case 4: return 2; // gris + alpha
        case 6: return 4; // RVBA
        default: return 0;
    }
}

} // namespace

bool decode_png(const std::string& bytes, render::Framebuffer& out, std::string& error) {
    error.clear();

    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const std::size_t size = bytes.size();

    if (size < 8 || std::memcmp(data, kSignature, 8) != 0) {
        error = "signature PNG absente";
        return false;
    }

    // --- Parcours des chunks ---
    std::uint32_t width = 0, height = 0;
    int bit_depth = 0, color_type = 0, interlace = 0;
    bool have_header = false;

    std::string compressed;   // concatenation des IDAT
    std::vector<std::uint8_t> palette;      // PLTE, triplets RVB
    std::vector<std::uint8_t> palette_alpha; // tRNS, un octet par index

    std::size_t pos = 8;
    while (pos + 8 <= size) {
        const std::uint32_t length = read_u32(data + pos);
        const std::uint8_t* type = data + pos + 4;

        // 12 = longueur + type + CRC. Le controle evite qu une longueur forgee
        // fasse sortir du tampon.
        if (length > size || pos + 12 + length > size) {
            error = "chunk PNG tronque ou longueur invalide";
            return false;
        }
        const std::uint8_t* payload = data + pos + 8;

        // Le CRC couvre le type et les donnees, pas la longueur.
        const std::uint32_t stored_crc = read_u32(data + pos + 8 + length);
        if (compress::crc32(type, length + 4) != stored_crc) {
            error = "CRC de chunk PNG incorrect, fichier corrompu";
            return false;
        }

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (length != 13) {
                error = "IHDR de taille inattendue";
                return false;
            }
            width = read_u32(payload);
            height = read_u32(payload + 4);
            bit_depth = payload[8];
            color_type = payload[9];
            // payload[10] = compression, payload[11] = filtre : une seule
            // valeur legale pour chacun, donc rien a decider.
            interlace = payload[12];
            have_header = true;

            if (width == 0 || height == 0 || width > 16384 || height > 16384) {
                error = "dimensions PNG invalides";
                return false;
            }
            if (bit_depth != 8) {
                error = "PNG " + std::to_string(bit_depth) +
                        " bits non supporte (8 bits par composante uniquement)";
                return false;
            }
            if (channels_for_color_type(color_type) == 0) {
                error = "type de couleur PNG inconnu : " + std::to_string(color_type);
                return false;
            }
            if (interlace != 0) {
                error = "PNG entrelace (Adam7) non supporte";
                return false;
            }
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            if (length % 3 != 0) {
                error = "PLTE de taille invalide";
                return false;
            }
            palette.assign(payload, payload + length);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            palette_alpha.assign(payload, payload + length);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            // Les donnees peuvent etre reparties sur plusieurs IDAT ; le flux
            // zlib ne commence a avoir un sens qu une fois tout concatene.
            compressed.append(reinterpret_cast<const char*>(payload), length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }

        pos += 12 + length;
    }

    if (!have_header) {
        error = "IHDR absent";
        return false;
    }
    if (compressed.empty()) {
        error = "aucune donnee IDAT";
        return false;
    }
    if (color_type == 3 && palette.empty()) {
        error = "PNG a palette sans PLTE";
        return false;
    }

    // --- Decompression ---
    std::string raw;
    if (!compress::inflate_zlib(compressed, raw, error)) {
        error = "flux IDAT illisible : " + error;
        return false;
    }

    const int channels = channels_for_color_type(color_type);
    const std::size_t stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    // Chaque ligne est precedee d un octet donnant son filtre.
    const std::size_t expected = (stride + 1) * static_cast<std::size_t>(height);
    if (raw.size() < expected) {
        error = "donnees PNG decompressees incompletes";
        return false;
    }

    // --- Defiltrage, ligne par ligne ---
    //
    // Les filtres sont differentiels : une ligne se reconstruit a partir des
    // octets deja reconstruits a sa gauche et de la ligne precedente, deja
    // reconstruite elle aussi. L ordre de parcours est donc impose.
    std::vector<std::uint8_t> image(stride * height);
    const auto* src = reinterpret_cast<const std::uint8_t*>(raw.data());

    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t filter = src[y * (stride + 1)];
        const std::uint8_t* line = src + y * (stride + 1) + 1;
        std::uint8_t* dst = image.data() + y * stride;
        const std::uint8_t* prev = (y > 0) ? (image.data() + (y - 1) * stride) : nullptr;

        for (std::size_t i = 0; i < stride; ++i) {
            const std::uint8_t a = (i >= static_cast<std::size_t>(channels))
                                       ? dst[i - channels]
                                       : 0; // gauche
            const std::uint8_t b = prev ? prev[i] : 0; // dessus
            const std::uint8_t c = (prev && i >= static_cast<std::size_t>(channels))
                                       ? prev[i - channels]
                                       : 0; // diagonale

            std::uint8_t value = line[i];
            switch (filter) {
                case 0: break;                                        // None
                case 1: value = static_cast<std::uint8_t>(value + a); break; // Sub
                case 2: value = static_cast<std::uint8_t>(value + b); break; // Up
                case 3: value = static_cast<std::uint8_t>(value + ((a + b) >> 1)); break; // Average
                case 4: value = static_cast<std::uint8_t>(value + paeth(a, b, c)); break; // Paeth
                default:
                    error = "filtre de ligne PNG inconnu : " + std::to_string(filter);
                    return false;
            }
            dst[i] = value;
        }
    }

    // --- Conversion vers BGRA ---
    out.resize(static_cast<int>(width), static_cast<int>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = image.data() + y * stride;
        std::uint32_t* dst = out.row(static_cast<int>(y));

        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * channels;
            std::uint32_t r = 0, g = 0, b = 0, a = 255;

            switch (color_type) {
                case 0: r = g = b = p[0]; break;
                case 2: r = p[0]; g = p[1]; b = p[2]; break;
                case 3: {
                    const std::size_t index = p[0];
                    if (index * 3 + 2 >= palette.size()) {
                        error = "index de palette hors limites";
                        return false;
                    }
                    r = palette[index * 3];
                    g = palette[index * 3 + 1];
                    b = palette[index * 3 + 2];
                    if (index < palette_alpha.size()) a = palette_alpha[index];
                    break;
                }
                case 4: r = g = b = p[0]; a = p[1]; break;
                case 6: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
                default: break;
            }
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return true;
}

bool load_png_file(const std::string& path, render::Framebuffer& out, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        error = "fichier image illisible ou absent : " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        error = "fichier image vide : " + path;
        return false;
    }

    std::string bytes(static_cast<std::size_t>(size), '\0');
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (read != bytes.size()) {
        error = "lecture incomplete de " + path;
        return false;
    }

    if (!decode_png(bytes, out, error)) {
        error = path + " : " + error;
        return false;
    }
    return true;
}

} // namespace gw::image
