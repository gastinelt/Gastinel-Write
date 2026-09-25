#include "doc/zip.h"

#include "compress/checksum.h"
#include "compress/inflate.h"

#include <cstring>

namespace gw::doc {
namespace {

constexpr std::uint32_t kSigLocal = 0x04034B50u;
constexpr std::uint32_t kSigCentral = 0x02014B50u;
constexpr std::uint32_t kSigEocd = 0x06054B50u;

constexpr std::uint16_t kMethodStored = 0;
constexpr std::uint16_t kMethodDeflate = 8;

// Le zip stocke ses entiers en little-endian, quelle que soit la machine.
// On les ecrit octet par octet plutot que de caster un pointeur : cest la
// seule facon dobtenir le meme fichier sur une machine big-endian, et cela
// evite un acces non aligne.
void put_u16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void put_u32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
    out.push_back(static_cast<char>((v >> 16) & 0xFFu));
    out.push_back(static_cast<char>((v >> 24) & 0xFFu));
}

bool get_u16(const std::string& d, std::size_t off, std::uint16_t& v) {
    if (off + 2 > d.size()) return false;
    v = static_cast<std::uint16_t>(static_cast<unsigned char>(d[off]) |
                                   (static_cast<unsigned char>(d[off + 1]) << 8));
    return true;
}

bool get_u32(const std::string& d, std::size_t off, std::uint32_t& v) {
    if (off + 4 > d.size()) return false;
    v = static_cast<std::uint32_t>(static_cast<unsigned char>(d[off])) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(d[off + 1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(d[off + 2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(d[off + 3])) << 24);
    return true;
}

} // namespace

// Le calcul lui-meme vit dans compress/, partage avec la validation des
// chunks PNG. On garde ce point d entree pour ne pas changer l API de doc/.
std::uint32_t crc32(const std::string& data) { return compress::crc32(data); }

// --- Ecriture -------------------------------------------------------------

void ZipWriter::add(const std::string& name, const std::string& content) {
    Entry e;
    e.name = name;
    e.content = content;
    e.crc = crc32(content);
    entries_.push_back(std::move(e));
}

std::string ZipWriter::build() const {
    std::string out;
    std::vector<std::uint32_t> offsets;
    offsets.reserve(entries_.size());

    // --- En-tetes locaux + donnees ---
    for (const Entry& e : entries_) {
        offsets.push_back(static_cast<std::uint32_t>(out.size()));

        put_u32(out, kSigLocal);
        put_u16(out, 10);             // version minimale pour "stored"
        put_u16(out, 0);              // aucun drapeau
        put_u16(out, kMethodStored);
        put_u16(out, 0);              // heure MS-DOS : non renseignee
        put_u16(out, 0x21);           // date MS-DOS : 1980-01-01, valeur minimale valide
        put_u32(out, e.crc);
        put_u32(out, static_cast<std::uint32_t>(e.content.size()));
        put_u32(out, static_cast<std::uint32_t>(e.content.size()));
        put_u16(out, static_cast<std::uint16_t>(e.name.size()));
        put_u16(out, 0);              // pas de champ "extra"
        out += e.name;
        out += e.content;
    }

    // --- Repertoire central ---
    const std::uint32_t central_offset = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        put_u32(out, kSigCentral);
        put_u16(out, 20);             // version de creation
        put_u16(out, 10);             // version minimale de lecture
        put_u16(out, 0);
        put_u16(out, kMethodStored);
        put_u16(out, 0);
        put_u16(out, 0x21);
        put_u32(out, e.crc);
        put_u32(out, static_cast<std::uint32_t>(e.content.size()));
        put_u32(out, static_cast<std::uint32_t>(e.content.size()));
        put_u16(out, static_cast<std::uint16_t>(e.name.size()));
        put_u16(out, 0);              // extra
        put_u16(out, 0);              // commentaire
        put_u16(out, 0);              // disque de depart
        put_u16(out, 0);              // attributs internes
        put_u32(out, 0);              // attributs externes
        put_u32(out, offsets[i]);
        out += e.name;
    }
    const std::uint32_t central_size = static_cast<std::uint32_t>(out.size()) - central_offset;

    // --- Fin de repertoire central ---
    put_u32(out, kSigEocd);
    put_u16(out, 0); // numero de disque
    put_u16(out, 0); // disque du repertoire central
    put_u16(out, static_cast<std::uint16_t>(entries_.size()));
    put_u16(out, static_cast<std::uint16_t>(entries_.size()));
    put_u32(out, central_size);
    put_u32(out, central_offset);
    put_u16(out, 0); // pas de commentaire darchive

    return out;
}

// --- Lecture --------------------------------------------------------------

bool ZipReader::open(std::string data, std::string& error) {
    data_ = std::move(data);
    entries_.clear();
    names_.clear();

    if (data_.size() < 22) {
        error = "archive trop courte pour contenir une fin de repertoire central";
        return false;
    }

    // La fin de repertoire central est en fin de fichier, mais sa position
    // exacte depend de la longueur du commentaire darchive. On la cherche donc
    // a rebours. Le commentaire fait au plus 65535 octets, do la borne.
    std::size_t eocd = std::string::npos;
    const std::size_t max_back = data_.size() < 65557 ? data_.size() : 65557;
    for (std::size_t back = 22; back <= max_back; ++back) {
        const std::size_t pos = data_.size() - back;
        std::uint32_t sig = 0;
        if (get_u32(data_, pos, sig) && sig == kSigEocd) {
            eocd = pos;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "fin de repertoire central introuvable : ce nest pas un zip";
        return false;
    }

    std::uint16_t count = 0;
    std::uint32_t central_offset = 0;
    if (!get_u16(data_, eocd + 10, count) || !get_u32(data_, eocd + 16, central_offset)) {
        error = "fin de repertoire central tronquee";
        return false;
    }

    std::size_t pos = central_offset;
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint32_t sig = 0;
        if (!get_u32(data_, pos, sig) || sig != kSigCentral) {
            error = "entree de repertoire central invalide";
            return false;
        }
        Entry e;
        std::uint16_t name_len = 0, extra_len = 0, comment_len = 0;
        if (!get_u16(data_, pos + 10, e.method) || !get_u32(data_, pos + 16, e.crc) ||
            !get_u32(data_, pos + 20, e.compressed_size) ||
            !get_u32(data_, pos + 24, e.uncompressed_size) ||
            !get_u16(data_, pos + 28, name_len) || !get_u16(data_, pos + 30, extra_len) ||
            !get_u16(data_, pos + 32, comment_len) ||
            !get_u32(data_, pos + 42, e.local_header_offset)) {
            error = "entree de repertoire central tronquee";
            return false;
        }
        if (pos + 46 + name_len > data_.size()) {
            error = "nom dentree hors archive";
            return false;
        }
        e.name = data_.substr(pos + 46, name_len);
        names_.push_back(e.name);
        entries_.push_back(std::move(e));

        pos += 46u + name_len + extra_len + comment_len;
    }
    return true;
}

bool ZipReader::extract(const std::string& name, std::string& out, std::string& error) const {
    for (const Entry& e : entries_) {
        if (e.name != name) continue;

        if (e.method != kMethodStored && e.method != kMethodDeflate) {
            error = "entree \"" + name + "\" : methode de compression " +
                    std::to_string(e.method) + " non geree (stored et deflate uniquement)";
            return false;
        }

        // Le repertoire central donne la position de len-tete local, mais pas
        // celle des donnees : les longueurs de nom et de champ extra peuvent
        // differer entre les deux en-tetes, il faut donc relire celles du local.
        const std::size_t lh = e.local_header_offset;
        std::uint32_t sig = 0;
        std::uint16_t name_len = 0, extra_len = 0;
        if (!get_u32(data_, lh, sig) || sig != kSigLocal ||
            !get_u16(data_, lh + 26, name_len) || !get_u16(data_, lh + 28, extra_len)) {
            error = "en-tete local invalide pour \"" + name + "\"";
            return false;
        }
        const std::size_t start = lh + 30u + name_len + extra_len;

        if (e.method == kMethodStored) {
            if (start + e.uncompressed_size > data_.size()) {
                error = "donnees hors archive pour \"" + name + "\"";
                return false;
            }
            out = data_.substr(start, e.uncompressed_size);
        } else {
            if (start + e.compressed_size > data_.size()) {
                error = "donnees compressees hors archive pour \"" + name + "\"";
                return false;
            }
            // Une entree zip contient du DEFLATE brut, sans len-tete zlib qui
            // enveloppe les flux PNG : cest lintegrite du zip, via son CRC-32,
            // qui remplace ici lAdler-32.
            const auto* raw = reinterpret_cast<const std::uint8_t*>(data_.data()) + start;
            if (!compress::inflate_raw(raw, e.compressed_size, out, error,
                                       e.uncompressed_size)) {
                error = "decompression de \"" + name + "\" impossible : " + error;
                return false;
            }
            if (out.size() != e.uncompressed_size) {
                error = "taille decompressee inattendue pour \"" + name + "\"";
                return false;
            }
        }

        // Le CRC-32 est la seule verification dintegrite du contenu ; il vaut
        // pour les deux methodes. Une valeur nulle annoncee signale en pratique
        // une entree vide ou un ecrivain qui ne la pas renseignee, on ne la
        // traite donc pas comme une erreur.
        if (e.crc != 0 && compress::crc32(out) != e.crc) {
            error = "CRC-32 incorrect pour \"" + name + "\" : donnees corrompues";
            return false;
        }
        return true;
    }
    error = "entree \"" + name + "\" absente de larchive";
    return false;
}

} // namespace gw::doc
