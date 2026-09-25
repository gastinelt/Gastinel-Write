// compress/checksum.h — sommes de controle des formats compresses.
//
// CRC-32 et Adler-32 vivent ici plutot que dans leurs consommateurs parce que
// les deux servent a plusieurs endroits : le CRC-32 valide les entrees zip et
// les chunks PNG, l Adler-32 valide l enveloppe zlib d un flux PNG.
//
// Module pur : aucune dependance sur l OS, sur le rendu ni sur doc/.
#pragma once

#include <cstdint>
#include <string>

namespace gw::compress {

// CRC-32 polynome 0xEDB88320, celui de zip, PNG et gzip.
std::uint32_t crc32(const std::string& data);
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

// Adler-32 (RFC 1950), la somme de controle de l enveloppe zlib.
// Moins robuste qu un CRC mais bien plus rapide a calculer, ce qui explique
// son usage dans zlib.
std::uint32_t adler32(const std::uint8_t* data, std::size_t size);

} // namespace gw::compress
