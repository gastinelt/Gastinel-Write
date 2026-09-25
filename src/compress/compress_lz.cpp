// compress/compress_lz.cpp — implementation compression LZ ultra-rapide.
#include "compress/compress_lz.h"

#include <algorithm>
#include <cstring>

namespace gw::compress {
namespace {

constexpr std::size_t kHashBits = 12;
constexpr std::size_t kHashSize = 1 << kHashBits; // 4096 entrees
constexpr std::size_t kMinMatch = 4;
constexpr std::size_t kMaxDistance = 65535;

inline std::uint32_t hash4(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return (v * 2654435761u) >> (32 - kHashBits);
}

} // namespace

std::vector<std::uint8_t> lz_compress(const void* src_data, std::size_t src_len) {
    std::vector<std::uint8_t> out;
    if (src_data == nullptr || src_len == 0) return out;

    const auto* in = static_cast<const std::uint8_t*>(src_data);
    out.reserve(src_len / 2 + 32);

    const std::uint8_t* ip = in;
    const std::uint8_t* const in_end = in + src_len;
    const std::uint8_t* const in_limit = (src_len >= 8) ? in_end - 8 : in;
    const std::uint8_t* anchor = in;

    std::uint32_t hash_table[kHashSize];
    std::memset(hash_table, 0, sizeof(hash_table));

    while (ip < in_limit) {
        const std::uint32_t h = hash4(ip);
        const std::uint8_t* ref = in + hash_table[h];
        hash_table[h] = static_cast<std::uint32_t>(ip - in);

        if (ref < ip && (ip - ref) <= static_cast<std::ptrdiff_t>(kMaxDistance) &&
            std::memcmp(ref, ip, kMinMatch) == 0) {
            // Correspondance trouvee ! Calculer la longueur exacte de correspondance
            const std::size_t lit_len = static_cast<std::size_t>(ip - anchor);
            const std::size_t offset = static_cast<std::size_t>(ip - ref);

            const std::uint8_t* match_ip = ip + kMinMatch;
            const std::uint8_t* match_ref = ref + kMinMatch;
            while (match_ip < in_end && *match_ip == *match_ref) {
                ++match_ip;
                ++match_ref;
            }
            const std::size_t match_len = static_cast<std::size_t>(match_ip - ip);

            // Emission du token
            std::uint8_t token = 0;
            if (lit_len < 15) {
                token |= static_cast<std::uint8_t>(lit_len << 4);
            } else {
                token |= 0xF0;
            }

            const std::size_t match_code = match_len - kMinMatch;
            if (match_code < 15) {
                token |= static_cast<std::uint8_t>(match_code);
            } else {
                token |= 0x0F;
            }
            out.push_back(token);

            // Longueur de literaux additionnelle
            if (lit_len >= 15) {
                std::size_t rem = lit_len - 15;
                while (rem >= 255) {
                    out.push_back(255);
                    rem -= 255;
                }
                out.push_back(static_cast<std::uint8_t>(rem));
            }

            // Copie des literaux
            out.insert(out.end(), anchor, anchor + lit_len);

            // Distance de match (2 octets, little-endian)
            out.push_back(static_cast<std::uint8_t>(offset & 0xFF));
            out.push_back(static_cast<std::uint8_t>((offset >> 8) & 0xFF));

            // Longueur de match additionnelle
            if (match_code >= 15) {
                std::size_t rem = match_code - 15;
                while (rem >= 255) {
                    out.push_back(255);
                    rem -= 255;
                }
                out.push_back(static_cast<std::uint8_t>(rem));
            }

            ip = match_ip;
            anchor = ip;
        } else {
            ++ip;
        }
    }

    // Derniers literaux jusqu'a la fin du tampon
    const std::size_t last_lit = static_cast<std::size_t>(in_end - anchor);
    if (last_lit > 0) {
        std::uint8_t token = 0;
        if (last_lit < 15) {
            token = static_cast<std::uint8_t>(last_lit << 4);
        } else {
            token = 0xF0;
        }
        out.push_back(token);

        if (last_lit >= 15) {
            std::size_t rem = last_lit - 15;
            while (rem >= 255) {
                out.push_back(255);
                rem -= 255;
            }
            out.push_back(static_cast<std::uint8_t>(rem));
        }
        out.insert(out.end(), anchor, anchor + last_lit);
    }

    return out;
}

bool lz_decompress(const std::uint8_t* src, std::size_t src_len, void* dst_data,
                   std::size_t uncompressed_len) {
    if (uncompressed_len == 0) return true;
    if (src == nullptr || src_len == 0 || dst_data == nullptr) return false;

    const std::uint8_t* ip = src;
    const std::uint8_t* const in_end = src + src_len;
    auto* op = static_cast<std::uint8_t*>(dst_data);
    const std::uint8_t* const out_end = op + uncompressed_len;

    while (ip < in_end && op < out_end) {
        const std::uint8_t token = *ip++;

        // 1. Literaux
        std::size_t lit_len = token >> 4;
        if (lit_len == 15) {
            while (ip < in_end && *ip == 255) {
                lit_len += 255;
                ++ip;
            }
            if (ip < in_end) lit_len += *ip++;
        }

        if (static_cast<std::size_t>(out_end - op) < lit_len ||
            static_cast<std::size_t>(in_end - ip) < lit_len) {
            return false;
        }
        std::memcpy(op, ip, lit_len);
        op += lit_len;
        ip += lit_len;

        if (op >= out_end) break; // Fin du bloc

        // 2. Correspondance (match)
        if (static_cast<std::size_t>(in_end - ip) < 2) return false;
        const std::size_t offset =
            static_cast<std::size_t>(ip[0]) | (static_cast<std::size_t>(ip[1]) << 8);
        ip += 2;
        if (offset == 0 || offset > static_cast<std::size_t>(op - static_cast<std::uint8_t*>(dst_data))) {
            return false;
        }

        std::size_t match_len = (token & 0x0F) + kMinMatch;
        if ((token & 0x0F) == 15) {
            while (ip < in_end && *ip == 255) {
                match_len += 255;
                ++ip;
            }
            if (ip < in_end) match_len += *ip++;
        }

        if (static_cast<std::size_t>(out_end - op) < match_len) return false;

        const std::uint8_t* ref = op - offset;
        for (std::size_t i = 0; i < match_len; ++i) {
            *op++ = *ref++;
        }
    }

    return op == out_end;
}

std::string lz_decompress_string(const std::uint8_t* src, std::size_t src_len,
                                 std::size_t uncompressed_len) {
    if (uncompressed_len == 0) return {};
    std::string out;
    out.resize(uncompressed_len);
    if (!lz_decompress(src, src_len, out.data(), uncompressed_len)) {
        return {};
    }
    return out;
}

} // namespace gw::compress
