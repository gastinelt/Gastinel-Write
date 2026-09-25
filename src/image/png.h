// image/png.h — decodeur PNG maison.
//
// Remplace le chemin GDI+ : le decodage passe desormais par notre propre
// inflate, ce qui donne un seul chemin de decompression pour tout le projet et
// supprime la derniere dependance a un codec systeme.
//
// Module pur : aucune dependance sur l OS ni sur Win32. C est ce qui le rend
// portable tel quel, contrairement a l ancien platform/image.cpp.
//
// SOUS-ENSEMBLE SUPPORTE, volontairement reduit a ce qui sert :
//   profondeur      : 8 bits par composante uniquement
//   types de couleur: 0 (gris), 2 (RVB), 3 (palette), 4 (gris+alpha), 6 (RVBA)
//   entrelacement   : aucun (Adam7 refuse avec un message clair)
//   chunks lus      : IHDR, PLTE, tRNS, IDAT, IEND
//
// Tout le reste (16 bits, Adam7, gamma, profils de couleur) est refuse
// explicitement plutot que mal interprete.
#pragma once

#include <string>

#include "render/bitmap.h"

namespace gw::image {

// Decode un PNG deja charge en memoire vers un Framebuffer BGRA a alpha droit.
bool decode_png(const std::string& bytes, render::Framebuffer& out, std::string& error);

// Lit le fichier puis le decode. Le detour par un tampon complet est sans
// consequence : les images manipulees ici font quelques dizaines de kilo-octets.
bool load_png_file(const std::string& path, render::Framebuffer& out, std::string& error);

} // namespace gw::image
