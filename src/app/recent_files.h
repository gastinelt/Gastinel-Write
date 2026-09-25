// app/recent_files.h — liste des notes recemment ouvertes.
//
// FORMAT DE PERSISTANCE : un fichier texte, un chemin absolu par ligne, encode
// en UTF-8, sans BOM, fins de ligne CRLF. Le plus recent en premier.
// Les lignes vides et celles commencant par '#' sont ignorees a la lecture,
// ce qui laisse la place a un commentaire ou a un futur en-tete de version.
//
// Pourquoi pas de JSON : il faudrait un parseur, donc soit une dependance
// externe (interdite), soit un parseur maison pour stocker... une liste de
// chaines. Un chemin par ligne est lisible a loeil, editable a la main,
// impossible a mal parser, et ne peut pas se corrompre partiellement.
// La seule chose quun chemin Windows ne peut pas contenir est justement un
// retour a la ligne, donc le separateur est sans ambiguite.
//
// EMPLACEMENT : %APPDATA%\Gastinel\Write\recent.txt
// (et non a cote du binaire : un executable peut etre installe dans un
// repertoire en lecture seule).
#pragma once

#include <string>
#include <vector>

namespace gw::app {

class RecentFiles {
public:
    // Nombre maximum dentrees conservees. Lecran daccueil affiche 2 colonnes
    // sur 7 lignes sans defilement, soit 14 cartes ; on en garde davantage
    // pour que le defilement ait un interet.
    static constexpr std::size_t kMaxEntries = 40;

    // Charge la liste. Un fichier absent nest pas une erreur : cest le cas
    // du premier lancement.
    void load();

    // Place `path` en tete, en supprimant un eventuel doublon, puis enregistre.
    //
    // Les deux methodes prennent leur argument PAR VALEUR : un appelant peut
    // tres naturellement passer un element de entries() lui-meme, et ces
    // methodes reordonnent ce vecteur. Une reference deviendrait invalide au
    // milieu de loperation.
    void touch(std::string path);

    // Retire une entree (fichier disparu, par exemple) et enregistre.
    void remove(std::string path);

    const std::vector<std::string>& entries() const { return entries_; }

    // Chemin du fichier de persistance, cree les dossiers au besoin.
    static std::string storage_path();

private:
    void save() const;

    std::vector<std::string> entries_;
};

// Nom de fichier seul, sans le chemin. Utilise comme titre de carte.
std::string file_name_of(const std::string& path);

// Extension en minuscules, avec le point (".txt", ".odt"), vide si absente.
std::string extension_of(const std::string& path);

} // namespace gw::app
