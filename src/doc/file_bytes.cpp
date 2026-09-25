#include "doc/file_bytes.h"

#include <cstdio>

namespace gw::doc {

bool read_file_binary(const std::string& path, std::string& out, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        error = "impossible douvrir en lecture : " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        error = "taille illisible : " + path;
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    const std::size_t read = size > 0 ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);

    if (read != out.size()) {
        error = "lecture incomplete : " + path;
        return false;
    }
    return true;
}

bool write_file_binary(const std::string& path, const std::string& data, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        error = "impossible douvrir en ecriture : " + path;
        return false;
    }
    const std::size_t written = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
    const bool ok = std::fclose(f) == 0 && written == data.size();
    if (!ok) {
        error = "ecriture incomplete : " + path;
        return false;
    }
    return true;
}

} // namespace gw::doc
