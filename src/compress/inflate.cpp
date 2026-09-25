#include "compress/inflate.h"

#include "compress/checksum.h"

#include <array>
#include <vector>

namespace gw::compress {
namespace {

// --- Tables de la RFC 1951 -------------------------------------------------

// Codes de longueur 257..285 : longueur de base, puis nombre de bits
// supplementaires a lire derriere le code.
constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                           15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                           67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// Codes de distance 0..29 : distance de base et bits supplementaires.
constexpr std::uint16_t kDistanceBase[30] = {1,    2,    3,    4,    5,    7,     9,
                                             13,   17,   25,   33,   49,   65,    97,
                                             129,  193,  257,  385,  513,  769,   1025,
                                             1537, 2049, 3073, 4097, 6145, 8193,  12289,
                                             16385, 24577};
constexpr std::uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,
                                             4, 4, 5, 5, 6, 6, 7,  7,  8,  8,
                                             9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Ordre, volontairement non sequentiel, dans lequel les longueurs de l alphabet
// des longueurs de code sont stockees. Les valeurs les plus souvent nulles sont
// placees en fin, pour que HCLEN puisse les tronquer.
constexpr std::uint8_t kCodeLengthOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                               11, 4,  12, 3, 13, 2, 14, 1, 15};

constexpr int kMaxBits = 15;

// Arbre de Huffman canonique, represente a la facon de « puff » : le nombre de
// codes par longueur, et les symboles tries par longueur puis par valeur.
//
// On ne construit pas de table de correspondance directe : le decodage se fait
// bit a bit. C est plus lent qu une table, mais les flux manipules ici font
// quelques dizaines de kilo-octets — la simplicite et l absence de table a
// dimensionner valent mieux que la vitesse.
struct Huffman {
    std::array<std::uint16_t, kMaxBits + 1> count{};
    std::vector<std::uint16_t> symbol;
};

// Retourne 0 si le code est complet, un nombre positif s il est incomplet
// (des combinaisons de bits ne correspondent a rien), un negatif s il est
// sur-souscrit (donc invalide).
int build_huffman(Huffman& h, const std::uint8_t* lengths, int count) {
    h.count.fill(0);
    for (int s = 0; s < count; ++s) h.count[lengths[s]]++;

    // Aucun symbole utilise : arbre vide, ce qui est legal.
    if (h.count[0] == count) return 0;

    // Verification de Kraft : a chaque longueur, le nombre de codes encore
    // disponibles double puis se voit retirer ceux qui sont consommes.
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }

    std::array<std::uint16_t, kMaxBits + 1> offsets{};
    for (int len = 1; len < kMaxBits; ++len) {
        offsets[len + 1] = static_cast<std::uint16_t>(offsets[len] + h.count[len]);
    }

    h.symbol.assign(static_cast<std::size_t>(count), 0);
    for (int s = 0; s < count; ++s) {
        if (lengths[s] != 0) h.symbol[offsets[lengths[s]]++] = static_cast<std::uint16_t>(s);
    }
    return left;
}

// --- Decodeur --------------------------------------------------------------

class Inflater {
public:
    Inflater(const std::uint8_t* data, std::size_t size, std::string& out, std::string& error,
             std::size_t max_output)
        : in_(data), in_size_(size), out_(out), error_(error), max_output_(max_output) {}

    bool run() {
        int last = 0;
        do {
            std::uint32_t value = 0;
            if (!bits(1, value)) return false;
            last = static_cast<int>(value);

            if (!bits(2, value)) return false;
            switch (value) {
                case 0: if (!stored_block()) return false; break;
                case 1: if (!fixed_block()) return false; break;
                case 2: if (!dynamic_block()) return false; break;
                default: return fail("type de bloc DEFLATE reserve (3)");
            }
        } while (!last);
        return true;
    }

    // Position, en octets, de la fin du flux DEFLATE. Le wrapper zlib en a
    // besoin pour retrouver l Adler-32 qui suit.
    //
    // `bits()` n aspire un octet que lorsqu il en manque, donc `bit_count_`
    // reste toujours strictement inferieur a 8 : les bits en tampon sont le
    // bourrage de fin du dernier octet, et `in_pos_` est deja la bonne
    // frontiere. La division ci-dessous vaut donc toujours zero aujourd hui ;
    // elle est ecrite quand meme pour que le calcul reste juste si `bits()`
    // venait a lire par mots plutot que par octets.
    std::size_t bytes_consumed() const {
        return in_pos_ - static_cast<std::size_t>(bit_count_ / 8);
    }

private:
    bool fail(const char* message) {
        error_ = message;
        return false;
    }

    // Lecture de `need` bits, du bit de poids faible vers le fort — c est la
    // convention de DEFLATE, a l inverse de celle des codes de Huffman
    // eux-memes, qui se lisent du poids fort au faible.
    bool bits(int need, std::uint32_t& out) {
        std::uint32_t value = bit_buffer_;
        while (bit_count_ < need) {
            if (in_pos_ >= in_size_) return fail("flux DEFLATE tronque");
            value |= static_cast<std::uint32_t>(in_[in_pos_++]) << bit_count_;
            bit_count_ += 8;
        }
        bit_buffer_ = value >> need;
        bit_count_ -= need;
        out = value & ((1u << need) - 1u);
        return true;
    }

    bool decode(const Huffman& h, int& symbol) {
        int code = 0;
        int first = 0;
        int index = 0;

        for (int len = 1; len <= kMaxBits; ++len) {
            std::uint32_t bit = 0;
            if (!bits(1, bit)) return false;
            code |= static_cast<int>(bit);

            const int count = h.count[len];
            if (code - first < count) {
                symbol = h.symbol[static_cast<std::size_t>(index + (code - first))];
                return true;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return fail("code Huffman invalide");
    }

    bool emit(std::uint8_t byte) {
        if (out_.size() >= max_output_) return fail("sortie decompressee trop volumineuse");
        out_.push_back(static_cast<char>(byte));
        return true;
    }

    bool stored_block() {
        // Un bloc non compresse commence sur une frontiere d octet.
        bit_buffer_ = 0;
        bit_count_ = 0;

        if (in_pos_ + 4 > in_size_) return fail("bloc stored tronque");
        const unsigned len = static_cast<unsigned>(in_[in_pos_]) |
                             (static_cast<unsigned>(in_[in_pos_ + 1]) << 8);
        const unsigned nlen = static_cast<unsigned>(in_[in_pos_ + 2]) |
                              (static_cast<unsigned>(in_[in_pos_ + 3]) << 8);
        in_pos_ += 4;

        // NLEN est le complement a un de LEN : c est la seule verification
        // d integrite d un bloc stored.
        if ((len ^ 0xFFFFu) != nlen) return fail("bloc stored : LEN et NLEN incoherents");
        if (in_pos_ + len > in_size_) return fail("bloc stored : donnees tronquees");
        if (out_.size() + len > max_output_) return fail("sortie decompressee trop volumineuse");

        out_.append(reinterpret_cast<const char*>(in_ + in_pos_), len);
        in_pos_ += len;
        return true;
    }

    bool codes(const Huffman& lencode, const Huffman& distcode) {
        for (;;) {
            int symbol = 0;
            if (!decode(lencode, symbol)) return false;

            if (symbol < 256) {
                if (!emit(static_cast<std::uint8_t>(symbol))) return false;
                continue;
            }
            if (symbol == 256) return true; // fin de bloc

            symbol -= 257;
            if (symbol >= 29) return fail("code de longueur invalide");

            std::uint32_t extra = 0;
            if (!bits(kLengthExtra[symbol], extra)) return false;
            const std::size_t length = kLengthBase[symbol] + extra;

            int dist_symbol = 0;
            if (!decode(distcode, dist_symbol)) return false;
            if (dist_symbol >= 30) return fail("code de distance invalide");

            if (!bits(kDistanceExtra[dist_symbol], extra)) return false;
            const std::size_t distance = kDistanceBase[dist_symbol] + extra;

            // Une distance ne peut pas pointer avant le debut de la sortie.
            // Sans ce test, un flux forge lirait hors du tampon.
            if (distance > out_.size()) return fail("distance de reference hors limites");
            if (out_.size() + length > max_output_) {
                return fail("sortie decompressee trop volumineuse");
            }

            // La copie doit se faire octet par octet : la source peut chevaucher
            // la destination, et c est meme le mecanisme qui permet a DEFLATE
            // de coder une repetition (distance 1, longueur 100).
            const std::size_t start = out_.size() - distance;
            for (std::size_t i = 0; i < length; ++i) {
                out_.push_back(out_[start + i]);
            }
        }
    }

    bool fixed_block() {
        // Arbres figes par la RFC, construits une seule fois.
        static Huffman lencode;
        static Huffman distcode;
        static bool ready = false;
        if (!ready) {
            std::uint8_t lengths[288];
            for (int i = 0; i < 144; ++i) lengths[i] = 8;
            for (int i = 144; i < 256; ++i) lengths[i] = 9;
            for (int i = 256; i < 280; ++i) lengths[i] = 7;
            for (int i = 280; i < 288; ++i) lengths[i] = 8;
            build_huffman(lencode, lengths, 288);

            for (int i = 0; i < 30; ++i) lengths[i] = 5;
            build_huffman(distcode, lengths, 30);
            ready = true;
        }
        return codes(lencode, distcode);
    }

    bool dynamic_block() {
        std::uint32_t value = 0;
        if (!bits(5, value)) return false;
        const int nlen = static_cast<int>(value) + 257;
        if (!bits(5, value)) return false;
        const int ndist = static_cast<int>(value) + 1;
        if (!bits(4, value)) return false;
        const int ncode = static_cast<int>(value) + 4;

        if (nlen > 286 || ndist > 30) return fail("en-tete de bloc dynamique invalide");

        // 1. Longueurs de l alphabet qui code les longueurs.
        std::uint8_t lengths[288 + 30]{};
        for (int i = 0; i < ncode; ++i) {
            if (!bits(3, value)) return false;
            lengths[kCodeLengthOrder[i]] = static_cast<std::uint8_t>(value);
        }
        for (int i = ncode; i < 19; ++i) lengths[kCodeLengthOrder[i]] = 0;

        Huffman lencode;
        if (build_huffman(lencode, lengths, 19) != 0) {
            return fail("alphabet des longueurs de code incomplet");
        }

        // 2. Longueurs des alphabets litteraux/longueurs et distances, elles
        //    memes codees, avec trois codes de repetition.
        int index = 0;
        while (index < nlen + ndist) {
            int symbol = 0;
            if (!decode(lencode, symbol)) return false;

            if (symbol < 16) {
                lengths[index++] = static_cast<std::uint8_t>(symbol);
                continue;
            }

            int repeat = 0;
            std::uint8_t value_to_repeat = 0;
            if (symbol == 16) {
                if (index == 0) return fail("repetition sans longueur precedente");
                value_to_repeat = lengths[index - 1];
                if (!bits(2, value)) return false;
                repeat = 3 + static_cast<int>(value);
            } else if (symbol == 17) {
                if (!bits(3, value)) return false;
                repeat = 3 + static_cast<int>(value);
            } else {
                if (!bits(7, value)) return false;
                repeat = 11 + static_cast<int>(value);
            }

            if (index + repeat > nlen + ndist) return fail("repetition de longueurs debordante");
            while (repeat-- > 0) lengths[index++] = value_to_repeat;
        }

        if (lengths[256] == 0) return fail("code de fin de bloc absent");

        Huffman literal_code;
        Huffman distance_code;

        // Un code incomplet n est tolere que s il ne comporte qu un symbole :
        // c est le cas legitime d un bloc qui n utilise qu une seule distance.
        int err = build_huffman(literal_code, lengths, nlen);
        if (err != 0 && (err < 0 || nlen != literal_code.count[0] + literal_code.count[1])) {
            return fail("alphabet litteral/longueur invalide");
        }
        err = build_huffman(distance_code, lengths + nlen, ndist);
        if (err != 0 && (err < 0 || ndist != distance_code.count[0] + distance_code.count[1])) {
            return fail("alphabet de distances invalide");
        }

        return codes(literal_code, distance_code);
    }

    const std::uint8_t* in_;
    std::size_t in_size_;
    std::size_t in_pos_ = 0;
    std::uint32_t bit_buffer_ = 0;
    int bit_count_ = 0;

    std::string& out_;
    std::string& error_;
    std::size_t max_output_;
};

} // namespace

// --- Interface publique ----------------------------------------------------

bool inflate_raw(const std::uint8_t* data, std::size_t size, std::string& out,
                 std::string& error, std::size_t expected_size, std::size_t max_output) {
    out.clear();
    error.clear();
    if (data == nullptr || size == 0) {
        error = "flux DEFLATE vide";
        return false;
    }
    if (expected_size > 0 && expected_size <= max_output) out.reserve(expected_size);

    Inflater inflater(data, size, out, error, max_output);
    if (!inflater.run()) {
        out.clear();
        return false;
    }
    return true;
}

bool inflate_raw(const std::string& data, std::string& out, std::string& error,
                 std::size_t expected_size, std::size_t max_output) {
    return inflate_raw(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), out,
                       error, expected_size, max_output);
}

bool inflate_zlib(const std::uint8_t* data, std::size_t size, std::string& out,
                  std::string& error, std::size_t max_output) {
    out.clear();
    error.clear();

    // 2 octets d en-tete + au moins 1 octet de donnees + 4 octets d Adler-32.
    if (data == nullptr || size < 6) {
        error = "flux zlib trop court";
        return false;
    }

    const std::uint8_t cmf = data[0];
    const std::uint8_t flg = data[1];

    if ((cmf & 0x0F) != 8) {
        error = "flux zlib : methode de compression inattendue (attendu deflate)";
        return false;
    }
    // Les deux octets d en-tete forment un entier multiple de 31 : c est le
    // controle de coherence prevu par la RFC 1950.
    if (((static_cast<unsigned>(cmf) << 8) | flg) % 31u != 0) {
        error = "flux zlib : en-tete incoherent";
        return false;
    }
    if ((flg & 0x20) != 0) {
        error = "flux zlib : dictionnaire predefini non supporte";
        return false;
    }

    Inflater inflater(data + 2, size - 2, out, error, max_output);
    if (!inflater.run()) {
        out.clear();
        return false;
    }

    // L Adler-32 suit immediatement le flux, aligne sur un octet.
    const std::size_t tail = 2 + inflater.bytes_consumed();
    if (tail + 4 > size) {
        error = "flux zlib : somme de controle absente";
        out.clear();
        return false;
    }
    const std::uint32_t stored = (static_cast<std::uint32_t>(data[tail]) << 24) |
                                 (static_cast<std::uint32_t>(data[tail + 1]) << 16) |
                                 (static_cast<std::uint32_t>(data[tail + 2]) << 8) |
                                 static_cast<std::uint32_t>(data[tail + 3]);
    const std::uint32_t computed =
        adler32(reinterpret_cast<const std::uint8_t*>(out.data()), out.size());
    if (stored != computed) {
        error = "flux zlib : Adler-32 incorrect, donnees corrompues";
        out.clear();
        return false;
    }
    return true;
}

bool inflate_zlib(const std::string& data, std::string& out, std::string& error,
                  std::size_t max_output) {
    return inflate_zlib(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), out,
                        error, max_output);
}

} // namespace gw::compress
