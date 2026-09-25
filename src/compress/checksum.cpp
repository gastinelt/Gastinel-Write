#include "compress/checksum.h"

namespace gw::compress {
namespace {

const std::uint32_t* crc_table() {
    // Table construite au premier appel. 256 entrees, soit 1 Kio, calculees
    // une fois pour toutes.
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        ready = true;
    }
    return table;
}

} // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    const std::uint32_t* table = crc_table();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32(const std::string& data) {
    return crc32(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
    // 65521 est le plus grand nombre premier inferieur a 2^16 ; c est ce qui
    // donne a l Adler-32 ses proprietes de detection.
    constexpr std::uint32_t kModulo = 65521u;

    std::uint32_t a = 1;
    std::uint32_t b = 0;

    // Le modulo n est applique que tous les 5552 octets : c est le plus grand
    // nombre d iterations avant qu un depassement de 32 bits soit possible.
    // Le faire a chaque octet serait correct mais nettement plus lent.
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t chunk = (size - offset < 5552) ? (size - offset) : 5552;
        for (std::size_t i = 0; i < chunk; ++i) {
            a += data[offset + i];
            b += a;
        }
        a %= kModulo;
        b %= kModulo;
        offset += chunk;
    }
    return (b << 16) | a;
}

} // namespace gw::compress
