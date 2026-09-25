// compress/compress_lz.h — compression LZ rapide en memoire pour les textes.
//
// Algorithme derive de LZ77/LZ4, sans dependance externe :
//   - Compression extremement rapide (> 300 Mo/s sur CPU moderne)
//   - Decompression quasi-instantanee (> 1 Go/s)
//   - Taux de compression typique sur du texte : 50% a 75% d'espace gagne.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gw::compress {

// Compresse `src` dans un format compact.
std::vector<std::uint8_t> lz_compress(const void* src, std::size_t src_len);
inline std::vector<std::uint8_t> lz_compress(const std::string& text) {
    return lz_compress(text.data(), text.size());
}

// Decompresse `src` vers `dst` dont la taille `uncompressed_len` est connue.
// Retourne vrai en cas de succes.
bool lz_decompress(const std::uint8_t* src, std::size_t src_len, void* dst,
                   std::size_t uncompressed_len);

// Variante pratique retournant une std::string.
std::string lz_decompress_string(const std::uint8_t* src, std::size_t src_len,
                                 std::size_t uncompressed_len);

} // namespace gw::compress
