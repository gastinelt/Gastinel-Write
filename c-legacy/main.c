/*
 * Gastinel Write - declinaison C ultra-legere.
 * C89, Win32 Unicode, GDI seul, cible Windows XP. Aucune dependance a la CRT :
 * l'executable n'importe que kernel32, user32, gdi32 et comdlg32.
 * Fichiers lus en UTF-8 / UTF-16 / ANSI, toujours enregistres en UTF-8 sans BOM
 * (meme format que la declinaison Rust/Tauri).
 */

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>

#define APP_NAME    L"Gastinel Write"
#define APP_SLOGAN  L"L'écriture, sans fard."
#define APP_LEGAL   L"Gastinel. Logiciel libre de droit."

#define IDC_EDIT        100
#define IDM_NEW         200
#define IDM_OPEN        201
#define IDM_SAVE        202
#define IDM_SAVEAS      203
#define IDM_EXIT        204
#define IDM_UNDO        210
#define IDM_CUT         211
#define IDM_COPY        212
#define IDM_PASTE       213
#define IDM_SELALL      214
#define IDM_WRAP        220
#define IDM_ABOUT       230

#define EDIT_MARGIN     12

static HINSTANCE g_hInst;
static HWND      g_hWnd;
static HWND      g_hEdit;
static HFONT     g_hFont;
static HANDLE    g_heap;
static WCHAR     g_path[MAX_PATH];
static BOOL      g_wrap = TRUE;

/* gcc peut generer des appels implicites a memset/memcpy meme sans CRT. */
void *memset(void *dst, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void *mem_alloc(DWORD n) { return HeapAlloc(g_heap, 0, n); }
static void  mem_free(void *p)  { if (p) HeapFree(g_heap, 0, p); }

static const WCHAR *base_name(const WCHAR *path)
{
    const WCHAR *p = path, *name = path;
    for (; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    return name;
}

static void update_title(void)
{
    WCHAR title[MAX_PATH + 64];
    BOOL dirty = (BOOL)SendMessage(g_hEdit, EM_GETMODIFY, 0, 0);
    wsprintf(title, L"%s%s - %s",
             dirty ? L"*" : L"",
             g_path[0] ? base_name(g_path) : L"Sans titre",
             APP_NAME);
    SetWindowText(g_hWnd, title);
}

static void error_box(const WCHAR *msg)
{
    MessageBox(g_hWnd, msg, APP_NAME, MB_OK | MB_ICONERROR);
}

/* Octets du fichier -> UTF-16. Ordre : BOM UTF-16LE, BOM UTF-8, UTF-8 valide, sinon page ANSI. */
static WCHAR *decode(const BYTE *src, DWORD len, DWORD *out_len)
{
    WCHAR *dst;
    int n;
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;

    if (len >= 2 && src[0] == 0xFF && src[1] == 0xFE) {
        len = (len - 2) / 2;
        dst = (WCHAR *)mem_alloc((len + 1) * sizeof(WCHAR));
        if (!dst) return NULL;
        memcpy(dst, src + 2, len * sizeof(WCHAR));
        *out_len = len;
        return dst;
    }
    if (len >= 3 && src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) {
        src += 3; len -= 3;
    }
    if (len == 0) {
        dst = (WCHAR *)mem_alloc(sizeof(WCHAR));
        *out_len = 0;
        return dst;
    }
    n = MultiByteToWideChar(cp, flags, (LPCSTR)src, (int)len, NULL, 0);
    if (n == 0) {
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, (LPCSTR)src, (int)len, NULL, 0);
        if (n == 0) return NULL;
    }
    dst = (WCHAR *)mem_alloc((DWORD)(n + 1) * sizeof(WCHAR));
    if (!dst) return NULL;
    MultiByteToWideChar(cp, flags, (LPCSTR)src, (int)len, dst, n);
    *out_len = (DWORD)n;
    return dst;
}

/* Le controle EDIT exige des fins de ligne CRLF : on normalise LF et CR isoles. */
static WCHAR *to_crlf(const WCHAR *src, DWORD len)
{
    DWORD i, extra = 0, out = 0;
    WCHAR *dst;
    for (i = 0; i < len; ++i) {
        if (src[i] == L'\n' && (i == 0 || src[i - 1] != L'\r')) ++extra;
        else if (src[i] == L'\r' && (i + 1 >= len || src[i + 1] != L'\n')) ++extra;
    }
    dst = (WCHAR *)mem_alloc((len + extra + 1) * sizeof(WCHAR));
    if (!dst) return NULL;
    for (i = 0; i < len; ++i) {
        WCHAR c = src[i];
        if ((c == L'\n' && (i == 0 || src[i - 1] != L'\r')) ||
            (c == L'\r' && (i + 1 >= len || src[i + 1] != L'\n'))) {
            dst[out++] = L'\r';
            dst[out++] = L'\n';
        } else {
            dst[out++] = c;
        }
    }
    dst[out] = 0;
    return dst;
}

static BOOL load_file(const WCHAR *path)
{
    HANDLE f;
    DWORD size, got, wlen = 0;
    BYTE *raw;
    WCHAR *wide, *text;

    f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { error_box(L"Impossible d'ouvrir le fichier."); return FALSE; }

    size = GetFileSize(f, NULL);
    raw = (BYTE *)mem_alloc(size + 1);
    if (size == INVALID_FILE_SIZE || !raw ||
        !ReadFile(f, raw, size, &got, NULL) || got != size) {
        CloseHandle(f);
        mem_free(raw);
        error_box(L"Lecture du fichier impossible.");
        return FALSE;
    }
    CloseHandle(f);

    wide = decode(raw, size, &wlen);
    mem_free(raw);
    text = wide ? to_crlf(wide, wlen) : NULL;
    mem_free(wide);
    if (!text) { error_box(L"Fichier illisible ou mémoire insuffisante."); return FALSE; }

    SetWindowText(g_hEdit, text);
    mem_free(text);

    lstrcpyn(g_path, path, MAX_PATH);
    SendMessage(g_hEdit, EM_SETMODIFY, FALSE, 0);
    update_title();
    return TRUE;
}

/* Ecriture atomique : fichier temporaire a cote de la cible, puis remplacement,
   pour ne jamais laisser un document a moitie ecrit en cas de coupure. */
static BOOL write_atomic(const WCHAR *path, const char *data, DWORD len)
{
    WCHAR tmp[MAX_PATH + 8];
    HANDLE f;
    DWORD written;
    BOOL ok;

    if (lstrlen(path) + 8 >= MAX_PATH) return FALSE;
    wsprintf(tmp, L"%s.gw-tmp", path);

    f = CreateFile(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(f, data, len, &written, NULL) && written == len && FlushFileBuffers(f);
    CloseHandle(f);

    if (ok) ok = MoveFileEx(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFile(tmp);
    return ok;
}

static BOOL save_file(const WCHAR *path)
{
    int wlen, len;
    WCHAR *text;
    char *utf8 = NULL;
    BOOL ok = FALSE;

    wlen = GetWindowTextLength(g_hEdit);
    text = (WCHAR *)mem_alloc((DWORD)(wlen + 1) * sizeof(WCHAR));
    if (!text) { error_box(L"Mémoire insuffisante."); return FALSE; }
    GetWindowText(g_hEdit, text, wlen + 1);

    len = wlen ? WideCharToMultiByte(CP_UTF8, 0, text, wlen, NULL, 0, NULL, NULL) : 0;
    if (wlen == 0 || len > 0) {
        utf8 = (char *)mem_alloc((DWORD)len + 1);
        if (utf8) {
            if (len) WideCharToMultiByte(CP_UTF8, 0, text, wlen, utf8, len, NULL, NULL);
            ok = write_atomic(path, utf8, (DWORD)len);
        }
    }
    mem_free(utf8);
    mem_free(text);

    if (!ok) { error_box(L"Impossible d'enregistrer le fichier."); return FALSE; }

    lstrcpyn(g_path, path, MAX_PATH);
    SendMessage(g_hEdit, EM_SETMODIFY, FALSE, 0);
    update_title();
    return TRUE;
}

static BOOL file_dialog(WCHAR *out, BOOL saving)
{
    OPENFILENAME ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner   = g_hWnd;
    ofn.lpstrFilter = L"Texte (*.txt)\0*.txt\0Tous les fichiers (*.*)\0*.*\0";
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags       = OFN_HIDEREADONLY | OFN_ENABLESIZING |
                      (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    return saving ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn);
}

static BOOL do_save_as(void)
{
    WCHAR path[MAX_PATH];
    lstrcpyn(path, g_path, MAX_PATH);
    return file_dialog(path, TRUE) && save_file(path);
}

static BOOL do_save(void)
{
    return g_path[0] ? save_file(g_path) : do_save_as();
}

/* Renvoie FALSE si l'utilisateur annule. */
static BOOL confirm_discard(void)
{
    int r;
    if (!SendMessage(g_hEdit, EM_GETMODIFY, 0, 0)) return TRUE;
    r = MessageBox(g_hWnd, L"Enregistrer les modifications ?", APP_NAME,
                   MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return FALSE;
    if (r == IDYES) return do_save();
    return TRUE;
}

static void do_new(void)
{
    if (!confirm_discard()) return;
    SetWindowText(g_hEdit, L"");
    g_path[0] = 0;
    SendMessage(g_hEdit, EM_SETMODIFY, FALSE, 0);
    update_title();
}

static void do_open(void)
{
    WCHAR path[MAX_PATH];
    if (!confirm_discard()) return;
    path[0] = 0;
    if (file_dialog(path, FALSE)) load_file(path);
}

static void layout_edit(void)
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    MoveWindow(g_hEdit, 0, 0, rc.right, rc.bottom, TRUE);
}

/* ES_AUTOHSCROLL ne peut pas changer apres creation : on recree le controle
   en conservant texte et etat de modification. */
static void create_edit(const WCHAR *initial, BOOL modified)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                  ES_AUTOVSCROLL | ES_NOHIDESEL;
    if (!g_wrap) style |= WS_HSCROLL | ES_AUTOHSCROLL;

    g_hEdit = CreateWindowEx(0, L"EDIT", initial, style,
                             0, 0, 0, 0, g_hWnd, (HMENU)IDC_EDIT, g_hInst, NULL);
    SendMessage(g_hEdit, EM_SETLIMITTEXT, 0, 0);  /* sous NT : ~2 Go au lieu de 32 Ko */
    SendMessage(g_hEdit, WM_SETFONT, (WPARAM)g_hFont, FALSE);
    SendMessage(g_hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELONG(EDIT_MARGIN, EDIT_MARGIN));
    SendMessage(g_hEdit, EM_SETMODIFY, modified, 0);
    layout_edit();
    SetFocus(g_hEdit);
}

static void toggle_wrap(void)
{
    int len;
    WCHAR *text;
    BOOL modified;

    len = GetWindowTextLength(g_hEdit);
    text = (WCHAR *)mem_alloc((DWORD)(len + 1) * sizeof(WCHAR));
    if (!text) { error_box(L"Mémoire insuffisante."); return; }
    GetWindowText(g_hEdit, text, len + 1);
    modified = (BOOL)SendMessage(g_hEdit, EM_GETMODIFY, 0, 0);

    DestroyWindow(g_hEdit);
    g_wrap = !g_wrap;
    create_edit(text, modified);
    mem_free(text);

    CheckMenuItem(GetMenu(g_hWnd), IDM_WRAP, g_wrap ? MF_CHECKED : MF_UNCHECKED);
}

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(),
          edit = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();

    AppendMenu(file, MF_STRING, IDM_NEW,    L"&Nouveau\tCtrl+N");
    AppendMenu(file, MF_STRING, IDM_OPEN,   L"&Ouvrir...\tCtrl+O");
    AppendMenu(file, MF_STRING, IDM_SAVE,   L"&Enregistrer\tCtrl+S");
    AppendMenu(file, MF_STRING, IDM_SAVEAS, L"Enregistrer &sous...\tCtrl+Maj+S");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_EXIT,   L"&Quitter");

    AppendMenu(edit, MF_STRING, IDM_UNDO,   L"&Annuler\tCtrl+Z");
    AppendMenu(edit, MF_SEPARATOR, 0, NULL);
    AppendMenu(edit, MF_STRING, IDM_CUT,    L"Co&uper\tCtrl+X");
    AppendMenu(edit, MF_STRING, IDM_COPY,   L"&Copier\tCtrl+C");
    AppendMenu(edit, MF_STRING, IDM_PASTE,  L"C&oller\tCtrl+V");
    AppendMenu(edit, MF_SEPARATOR, 0, NULL);
    AppendMenu(edit, MF_STRING, IDM_SELALL, L"&Tout sélectionner\tCtrl+A");

    AppendMenu(view, MF_STRING | MF_CHECKED, IDM_WRAP, L"&Retour à la ligne");

    AppendMenu(help, MF_STRING, IDM_ABOUT,  L"À &propos");

    AppendMenu(bar, MF_POPUP, (UINT_PTR)file, L"&Fichier");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)edit, L"É&dition");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)view, L"&Affichage");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)help, L"&?");
    return bar;
}

static HACCEL build_accels(void)
{
    ACCEL a[5];
    a[0].fVirt = FCONTROL | FVIRTKEY; a[0].key = 'N'; a[0].cmd = IDM_NEW;
    a[1].fVirt = FCONTROL | FVIRTKEY; a[1].key = 'O'; a[1].cmd = IDM_OPEN;
    a[2].fVirt = FCONTROL | FVIRTKEY; a[2].key = 'S'; a[2].cmd = IDM_SAVE;
    a[3].fVirt = FCONTROL | FVIRTKEY; a[3].key = 'A'; a[3].cmd = IDM_SELALL;
    a[4].fVirt = FCONTROL | FSHIFT | FVIRTKEY; a[4].key = 'S'; a[4].cmd = IDM_SAVEAS;
    return CreateAcceleratorTable(a, 5);
}

static HFONT create_font(void)
{
    HDC dc = GetDC(NULL);
    int height = -MulDiv(11, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(NULL, dc);
    /* Tahoma est la police d'interface de XP ; ClearType s'applique s'il est active. */
    return CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hWnd = hwnd;
        g_hFont = create_font();
        create_edit(L"", FALSE);
        return 0;

    case WM_SIZE:
        layout_edit();
        return 0;

    case WM_SETFOCUS:
        SetFocus(g_hEdit);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_EDIT) {
            if (HIWORD(wParam) == EN_CHANGE) update_title();
            else if (HIWORD(wParam) == EN_ERRSPACE || HIWORD(wParam) == EN_MAXTEXT)
                error_box(L"Mémoire insuffisante pour ce texte.");
            return 0;
        }
        switch (LOWORD(wParam)) {
        case IDM_NEW:    do_new(); break;
        case IDM_OPEN:   do_open(); break;
        case IDM_SAVE:   do_save(); break;
        case IDM_SAVEAS: do_save_as(); break;
        case IDM_EXIT:   SendMessage(hwnd, WM_CLOSE, 0, 0); break;
        case IDM_UNDO:   SendMessage(g_hEdit, EM_UNDO, 0, 0); break;
        case IDM_CUT:    SendMessage(g_hEdit, WM_CUT, 0, 0); break;
        case IDM_COPY:   SendMessage(g_hEdit, WM_COPY, 0, 0); break;
        case IDM_PASTE:  SendMessage(g_hEdit, WM_PASTE, 0, 0); break;
        case IDM_SELALL: SendMessage(g_hEdit, EM_SETSEL, 0, -1); break;
        case IDM_WRAP:   toggle_wrap(); break;
        case IDM_ABOUT:
            MessageBox(hwnd, APP_NAME L"\n\n" APP_SLOGAN L"\n\n" APP_LEGAL,
                       L"À propos", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;

    case WM_CLOSE:
        if (confirm_discard()) DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        DeleteObject(g_hFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* Premier argument de la ligne de commande (gere les guillemets). */
static void first_arg(const WCHAR *cmd, WCHAR *out)
{
    int n = 0;
    WCHAR end = L' ';
    if (*cmd == L'"') { ++cmd; while (*cmd && *cmd != L'"') ++cmd; if (*cmd) ++cmd; }
    else while (*cmd && *cmd != L' ') ++cmd;
    while (*cmd == L' ') ++cmd;
    if (*cmd == L'"') { end = L'"'; ++cmd; }
    while (*cmd && *cmd != end && n < MAX_PATH - 1) out[n++] = *cmd++;
    out[n] = 0;
}

static int run(void)
{
    WNDCLASS wc;
    MSG msg;
    HACCEL accels;
    WCHAR arg[MAX_PATH];

    g_hInst = GetModuleHandle(NULL);
    g_heap  = GetProcessHeap();

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = L"GastinelWriteLegacy";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClass(&wc)) return 1;

    if (!CreateWindow(wc.lpszClassName, APP_NAME, WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                      NULL, build_menu(), g_hInst, NULL))
        return 1;

    first_arg(GetCommandLine(), arg);
    if (arg[0]) load_file(arg);
    else update_title();

    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);

    accels = build_accels();
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!TranslateAccelerator(g_hWnd, accels, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    DestroyAcceleratorTable(accels);
    return (int)msg.wParam;
}

/* Point d'entree sans CRT. */
void WinMainCRTStartup(void)
{
    ExitProcess((UINT)run());
}
