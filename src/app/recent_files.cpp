#include "app/recent_files.h"

#include "doc/file_bytes.h"
#include "platform/log.h"
#include "platform/system.h"

#include <algorithm>

namespace gw::app {

std::string RecentFiles::storage_path() {
    // Le repertoire est cree au besoin par la couche systeme ; ici on ne
    // connait que le nom du produit et celui du fichier.
    const std::string dir = platform::user_data_directory("Gastinel", "Write");
    if (dir.empty()) return {};
    return dir + "\\recent.txt";
}

void RecentFiles::load() {
    entries_.clear();

    const std::string path = storage_path();
    if (path.empty()) return;

    std::string data, error;
    if (!doc::read_file_binary(path, data, error)) {
        // Premier lancement : le fichier nexiste pas. Ce nest pas une erreur.
        return;
    }

    // Le fichier est ecrit sans BOM, mais il est prevu pour etre editable a la
    // main : un editeur Windows peut en ajouter un. Sans ce retrait, le BOM se
    // collerait au premier caractere et la premiere ligne serait illisible.
    std::size_t start = 0;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        start = 3;
    }

    std::string line;
    for (std::size_t i = start; i <= data.size(); ++i) {
        const char c = i < data.size() ? data[i] : '\n';
        if (c == '\n' || c == '\r') {
            if (!line.empty() && line[0] != '#') {
                if (entries_.size() < kMaxEntries) entries_.push_back(line);
            }
            line.clear();
        } else {
            line.push_back(c);
        }
    }
}

void RecentFiles::save() const {
    const std::string path = storage_path();
    if (path.empty()) return;

    std::string data = "# Gastinel Write - notes recemment ouvertes, la plus recente en tete\r\n";
    for (const std::string& e : entries_) data += e + "\r\n";

    std::string error;
    if (!doc::write_file_binary(path, data, error)) {
        platform::log_error("Projets recents : enregistrement impossible (%s)", error.c_str());
    }
}

void RecentFiles::touch(std::string path) {
    if (path.empty()) return;

    // La comparaison est sensible a la casse alors que Windows ne lest pas :
    // deux graphies du meme chemin creeraient donc deux entrees. Cest un
    // defaut connu et mineur — les chemins viennent de lexplorateur, qui rend
    // toujours la meme graphie.
    entries_.erase(std::remove(entries_.begin(), entries_.end(), path), entries_.end());
    entries_.insert(entries_.begin(), path);
    if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
    save();
}

void RecentFiles::remove(std::string path) {
    const auto it = std::remove(entries_.begin(), entries_.end(), path);
    if (it == entries_.end()) return;
    entries_.erase(it, entries_.end());
    save();
}

std::string file_name_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string extension_of(const std::string& path) {
    const std::string name = file_name_of(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return {};

    std::string ext = name.substr(dot);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

} // namespace gw::app
