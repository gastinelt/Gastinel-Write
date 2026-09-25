// compress/inflate.h — decompression DEFLATE maison (RFC 1951).
//
// Un seul decodeur pour tout le projet : les entrees zip compressees d un
// .odt (DEFLATE brut) et les flux d image PNG (DEFLATE enveloppe de zlib).
// C est ce qui permet de se passer d une bibliotheque systeme pour le PNG tout
// en levant la limite de lecture des .odt produits par LibreOffice.
//
// DECOMPRESSION SEULEMENT. L ecriture continue d utiliser la methode zip
// "stored", sans compression : nos fichiers texte sont minuscules et le gain
// de taille ne justifierait pas d ecrire un compresseur (Huffman + LZ77 cote
// encodage, avec ses propres choix d heuristiques).
//
// Module pur : aucune dependance sur l OS, sur le rendu ni sur doc/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gw::compress {

// Garde-fou par defaut sur la taille produite. Un flux DEFLATE court peut
// decrire une sortie enorme ; sans borne, un fichier malveillant epuiserait la
// memoire. 64 Mio depasse de tres loin tout ce que ce programme manipule.
constexpr std::size_t kDefaultMaxOutput = 64u * 1024u * 1024u;

// Flux DEFLATE brut, tel qu on le trouve dans une entree zip de methode 8.
//
// `expected_size` sert d indication de reservation quand elle est connue (le
// repertoire zip la donne) ; passer 0 laisse le tampon croitre tout seul.
// Retourne false et renseigne `error` sur flux invalide, tronque ou trop long.
bool inflate_raw(const std::uint8_t* data, std::size_t size, std::string& out,
                 std::string& error, std::size_t expected_size = 0,
                 std::size_t max_output = kDefaultMaxOutput);

bool inflate_raw(const std::string& data, std::string& out, std::string& error,
                 std::size_t expected_size = 0, std::size_t max_output = kDefaultMaxOutput);

// Flux zlib (RFC 1950) : deux octets d en-tete, le flux DEFLATE, puis un
// Adler-32 en gros-boutien. C est ce que contiennent les chunks IDAT d un PNG.
//
// L en-tete et la somme de controle sont verifies : c est la seule facon de
// distinguer un fichier corrompu d un fichier simplement inattendu.
bool inflate_zlib(const std::uint8_t* data, std::size_t size, std::string& out,
                  std::string& error, std::size_t max_output = kDefaultMaxOutput);

bool inflate_zlib(const std::string& data, std::string& out, std::string& error,
                  std::size_t max_output = kDefaultMaxOutput);

} // namespace gw::compress
