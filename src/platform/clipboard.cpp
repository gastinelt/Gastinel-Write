#include "platform/clipboard.h"

#include "platform/unicode.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace gw::platform {
namespace {

// Le presse-papiers doit etre referme quoi qu il arrive : un OpenClipboard
// laisse ouvert bloque tous les autres processus du systeme, y compris
// l explorateur. C est le genre de fuite qu on ne remarque qu une fois la
// machine dans un etat bizarre, donc elle est confiee au destructeur plutot
// qu a un chemin de sortie a ne pas oublier.
class ClipboardSession {
public:
    explicit ClipboardSession(HWND owner) : open_(OpenClipboard(owner) != FALSE) {}
    ~ClipboardSession() {
        if (open_) CloseClipboard();
    }
    ClipboardSession(const ClipboardSession&) = delete;
    ClipboardSession& operator=(const ClipboardSession&) = delete;

    bool is_open() const { return open_; }

private:
    bool open_ = false;
};

} // namespace

bool clipboard_set_text(const std::string& text) {
    const std::wstring wide = to_wide(text);

    // Le bloc doit etre GMEM_MOVEABLE : SetClipboardData en prend la propriete
    // et le liberera lui-meme. Un bloc alloue autrement serait libere deux fois.
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    const HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (block == nullptr) return false;

    auto* dst = static_cast<wchar_t*>(GlobalLock(block));
    if (dst == nullptr) {
        GlobalFree(block);
        return false;
    }
    std::memcpy(dst, wide.c_str(), bytes);
    GlobalUnlock(block);

    ClipboardSession session(nullptr);
    if (!session.is_open()) {
        GlobalFree(block);
        return false;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, block) == nullptr) {
        // La propriete n a pas ete transferee : c est a nous de liberer.
        GlobalFree(block);
        return false;
    }
    return true;
}

bool clipboard_get_text(std::string& text_out) {
    ClipboardSession session(nullptr);
    if (!session.is_open()) return false;

    const HANDLE block = GetClipboardData(CF_UNICODETEXT);
    if (block == nullptr) return false;

    const auto* src = static_cast<const wchar_t*>(GlobalLock(block));
    if (src == nullptr) return false;
    const std::string utf8 = to_utf8(src);
    GlobalUnlock(block);

    text_out.clear();
    text_out.reserve(utf8.size());
    for (const char c : utf8) {
        if (c != '\r') text_out.push_back(c);
    }
    return true;
}

} // namespace gw::platform
