// doc/zip.h — lecture/ecriture zip minimale.
//
// Juste ce quil faut pour porter un .odt, sans dependance externe (ni zlib,
// ni libzip, ni minizip).
//
// ECRITURE : toutes les entrees sont ecrites en methode "stored" (0),
// cest-a-dire sans compression.
//
// LECTURE : "stored" (0) et "deflate" (8). Le deflate passe par le decodeur
// maison de compress/, ce qui permet de relire les .odt produits par
// LibreOffice ou Word, qui compressent systematiquement.
//
// COMPROMIS ASSUME cote ecriture :
//   - Ecrire du deflate demanderait un compresseur maison complet (arbres de
//     Huffman, fenetre LZ77) : plusieurs centaines de lignes pour gagner
//     quelques kilo-octets sur des fichiers texte deja minuscules.
//   - "Stored" est une methode parfaitement legale du format zip ; une archive
//     entierement stored souvre normalement partout.
//   - La contrainte OpenDocument sur `mimetype` (premier, non compresse) est
//     donc satisfaite par construction.
// Le jour ou la taille comptera, seul lecrivain aura a changer : le lecteur,
// lui, sait deja decompresser.
//
// Limites : pas de zip64 (donc 4 Gio et 65535 entrees au maximum), pas de
// chiffrement, pas de commentaire darchive, pas de descripteur de donnees.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gw::doc {

// --- Ecriture -------------------------------------------------------------

class ZipWriter {
public:
    // Ajoute une entree. Lordre des appels est lordre dans larchive, ce qui
    // compte pour `mimetype` qui doit etre en premier.
    void add(const std::string& name, const std::string& content);

    // Serialise larchive complete.
    std::string build() const;

private:
    struct Entry {
        std::string name;
        std::string content;
        std::uint32_t crc = 0;
    };
    std::vector<Entry> entries_;
};

// --- Lecture --------------------------------------------------------------

class ZipReader {
public:
    // Parse le repertoire central. `error` est renseigne en cas dechec.
    bool open(std::string data, std::string& error);

    // Extrait une entree par son nom exact. Retourne false si absente, ou si
    // sa methode de compression nest ni stored (0) ni deflate (8).
    bool extract(const std::string& name, std::string& out, std::string& error) const;

    const std::vector<std::string>& entry_names() const { return names_; }

private:
    struct Entry {
        std::string name;
        std::uint16_t method = 0;
        std::uint32_t crc = 0;
        std::uint32_t compressed_size = 0;
        std::uint32_t uncompressed_size = 0;
        std::uint32_t local_header_offset = 0;
    };

    std::string data_;
    std::vector<Entry> entries_;
    std::vector<std::string> names_;
};

// Expose pour les tests : CRC-32 (polynome zip standard).
std::uint32_t crc32(const std::string& data);

} // namespace gw::doc
