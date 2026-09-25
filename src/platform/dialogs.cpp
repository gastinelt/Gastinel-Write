#include "platform/dialogs.h"

#include "platform/unicode.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commdlg.h>

#include <algorithm>

namespace gw::platform {
bool prompt_open_document(NativeHandle owner, std::string& path_out) {
    wchar_t buffer[MAX_PATH]{};

    // Le filtre est une suite de paires de chaines terminees par un double
    // caractere nul : cest le format attendu par l API, pas une chaine
    // ordinaire.
    static const wchar_t kFilter[] =
        L"Notes Gastinel Write (*.txt;*.odt)\0*.txt;*.odt\0"
        L"Texte brut (*.txt)\0*.txt\0"
        L"Document ODT (*.odt)\0*.odt\0"
        L"Tous les fichiers (*.*)\0*.*\0\0";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(owner);
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Ouvrir une note";
    // OFN_FILEMUSTEXIST evite davoir a gerer le cas dun chemin saisi a la
    // main qui nexiste pas. OFN_NOCHANGEDIR empeche le dialogue de modifier
    // le repertoire courant du processus, ce qui casserait les chemins
    // relatifs utilises ailleurs.
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return false; // annulation, ou erreur

    path_out = to_utf8(buffer);
    return !path_out.empty();
}

bool prompt_save_document(NativeHandle owner, const std::string& suggested_name,
                          std::string& path_out) {
    wchar_t buffer[MAX_PATH]{};
    const std::wstring initial = to_wide(suggested_name);
    // Copie bornee a la main : le tampon doit rester termine par un nul meme
    // si le nom propose depasse MAX_PATH, et les variantes _s de la
    // bibliotheque ne sont pas disponibles de la meme facon sur les deux
    // chaines de compilation visees.
    const std::size_t copied = std::min(initial.size(), static_cast<std::size_t>(MAX_PATH) - 1);
    std::copy_n(initial.begin(), copied, buffer);
    buffer[copied] = L'\0';

    static const wchar_t kFilter[] =
        L"Texte brut (*.txt)\0*.txt\0"
        L"Document ODT (*.odt)\0*.odt\0\0";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(owner);
    ofn.lpstrFilter = kFilter;
    // Index 2 : l ODT est le format qui conserve le plus d information. Le
    // .txt reste a un clic, mais ce n est pas lui qu on propose par defaut.
    ofn.nFilterIndex = 2;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Enregistrer la note";
    ofn.lpstrDefExt = L"odt";
    // OFN_OVERWRITEPROMPT : c est Windows qui pose la question d ecrasement,
    // avec la formulation et les boutons que l utilisateur connait.
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) return false;

    path_out = to_utf8(buffer);
    return !path_out.empty();
}

ConfirmResult prompt_discard_changes(NativeHandle owner, const std::string& document_name) {
    const std::wstring text =
        to_wide("« " + document_name +
                " » a été modifiée.\n\nEnregistrer les modifications ?");
    const int answer = MessageBoxW(static_cast<HWND>(owner), text.c_str(), L"Gastinel Write",
                                   MB_YESNOCANCEL | MB_ICONWARNING);
    if (answer == IDYES) return ConfirmResult::Yes;
    if (answer == IDNO) return ConfirmResult::No;
    return ConfirmResult::Cancel; // IDCANCEL, Echap, ou echec de creation
}

bool prompt_open_font(NativeHandle owner, std::string& path_out) {
    wchar_t buffer[MAX_PATH]{};

    static const wchar_t kFilter[] =
        L"Police TrueType (*.ttf)\0*.ttf\0"
        L"Tous les fichiers (*.*)\0*.*\0\0";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(owner);
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Importer une police";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return false;

    path_out = to_utf8(buffer);
    return !path_out.empty();
}

void show_error_message(const std::string& title, const std::string& text) {
    MessageBoxW(nullptr, to_wide(text).c_str(), to_wide(title).c_str(), MB_ICONERROR | MB_OK);
}

} // namespace gw::platform
