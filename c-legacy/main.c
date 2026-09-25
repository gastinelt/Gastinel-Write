/*
 * Gastinel Write - declinaison C ultra-legere.
 * C89, Win32 ANSI, GDI seul. Aucune dependance a la CRT : l'executable
 * n'importe que kernel32, user32, gdi32 et comdlg32 (presents depuis Windows 95).
 */

#include <windows.h>
#include <commdlg.h>

#define APP_NAME    "Gastinel Write"
#define APP_SLOGAN  "L'\xE9" "criture, sans fard."
#define APP_LEGAL   "Gastinel. Logiciel libre de droit."

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
static char      g_path[MAX_PATH];
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

static const char *base_name(const char *path)
{
    const char *p = path, *name = path;
    for (; *p; ++p)
        if (*p == '\\' || *p == '/') name = p + 1;
    return name;
}

static void update_title(void)
{
    char title[MAX_PATH + 64];
    BOOL dirty = (BOOL)SendMessage(g_hEdit, EM_GETMODIFY, 0, 0);
    wsprintf(title, "%s%s - " APP_NAME,
             dirty ? "*" : "",
             g_path[0] ? base_name(g_path) : "Sans titre");
    SetWindowText(g_hWnd, title);
}

static void error_box(const char *msg)
{
    MessageBox(g_hWnd, msg, APP_NAME, MB_OK | MB_ICONERROR);
}

/* Le controle EDIT exige des fins de ligne CRLF : on normalise LF et CR isoles. */
static char *to_crlf(const char *src, DWORD len)
{
    DWORD i, extra = 0, out = 0;
    char *dst;
    for (i = 0; i < len; ++i) {
        if (src[i] == '\n' && (i == 0 || src[i - 1] != '\r')) ++extra;
        else if (src[i] == '\r' && (i + 1 >= len || src[i + 1] != '\n')) ++extra;
    }
    dst = (char *)HeapAlloc(GetProcessHeap(), 0, len + extra + 1);
    if (!dst) return NULL;
    for (i = 0; i < len; ++i) {
        char c = src[i];
        if (c == '\n' && (i == 0 || src[i - 1] != '\r')) {
            dst[out++] = '\r'; dst[out++] = '\n';
        } else if (c == '\r' && (i + 1 >= len || src[i + 1] != '\n')) {
            dst[out++] = '\r'; dst[out++] = '\n';
        } else {
            dst[out++] = c;
        }
    }
    dst[out] = '\0';
    return dst;
}

static BOOL load_file(const char *path)
{
    HANDLE f;
    DWORD size, got;
    char *raw, *text;
    const char *start;

    f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { error_box("Impossible d'ouvrir le fichier."); return FALSE; }

    size = GetFileSize(f, NULL);
    raw = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!raw || !ReadFile(f, raw, size, &got, NULL) || got != size) {
        CloseHandle(f);
        if (raw) HeapFree(GetProcessHeap(), 0, raw);
        error_box("Lecture du fichier impossible.");
        return FALSE;
    }
    CloseHandle(f);

    start = raw;
    if (size >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        start += 3; size -= 3;  /* BOM UTF-8 ignore */
    }

    text = to_crlf(start, size);
    HeapFree(GetProcessHeap(), 0, raw);
    if (!text) { error_box("M\xE9moire insuffisante."); return FALSE; }

    SetWindowText(g_hEdit, text);
    HeapFree(GetProcessHeap(), 0, text);

    lstrcpyn(g_path, path, MAX_PATH);
    SendMessage(g_hEdit, EM_SETMODIFY, FALSE, 0);
    update_title();
    return TRUE;
}

static BOOL save_file(const char *path)
{
    HANDLE f;
    int len;
    DWORD written;
    char *text;

    len = GetWindowTextLength(g_hEdit);
    text = (char *)HeapAlloc(GetProcessHeap(), 0, len + 1);
    if (!text) { error_box("M\xE9moire insuffisante."); return FALSE; }
    GetWindowText(g_hEdit, text, len + 1);

    f = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, text);
        error_box("Impossible d'enregistrer le fichier.");
        return FALSE;
    }
    if (!WriteFile(f, text, (DWORD)len, &written, NULL) || written != (DWORD)len) {
        CloseHandle(f);
        HeapFree(GetProcessHeap(), 0, text);
        error_box("\xC9" "criture incompl\xE8te.");
        return FALSE;
    }
    CloseHandle(f);
    HeapFree(GetProcessHeap(), 0, text);

    lstrcpyn(g_path, path, MAX_PATH);
    SendMessage(g_hEdit, EM_SETMODIFY, FALSE, 0);
    update_title();
    return TRUE;
}

static BOOL file_dialog(char *out, BOOL saving)
{
    OPENFILENAME ofn;
    memset(&ofn, 0, sizeof ofn);
    /* Taille "pre-2000" : acceptee par tous les comdlg32, de Windows 95 a aujourd'hui. */
    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner   = g_hWnd;
    ofn.lpstrFilter = "Texte (*.txt)\0*.txt\0Tous les fichiers (*.*)\0*.*\0";
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = "txt";
    ofn.Flags       = OFN_HIDEREADONLY |
                      (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    return saving ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn);
}

static BOOL do_save_as(void)
{
    char path[MAX_PATH];
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
    r = MessageBox(g_hWnd, "Enregistrer les modifications ?", APP_NAME,
                   MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return FALSE;
    if (r == IDYES) return do_save();
    return TRUE;
}

static void do_new(void)
{
    if (!confirm_discard()) return;
    SetWindowText(g_hEdit, "");
    g_path[0] = '\0';
    SendMessage(g_hEdit, EM_SETMODIFY, FALSE, 0);
    update_title();
}

static void do_open(void)
{
    char path[MAX_PATH];
    if (!confirm_discard()) return;
    path[0] = '\0';
    if (file_dialog(path, FALSE)) load_file(path);
}

static void layout_edit(void)
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    MoveWindow(g_hEdit, 0, 0, rc.right, rc.bottom, TRUE);
}

/* ES_AUTOHSCROLL ne peut pas changer apres creation : on recree le controle
   en conservant texte, selection et etat de modification. */
static void create_edit(const char *initial, BOOL modified)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                  ES_AUTOVSCROLL | ES_NOHIDESEL;
    if (!g_wrap) style |= WS_HSCROLL | ES_AUTOHSCROLL;

    g_hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", initial, style,
                             0, 0, 0, 0, g_hWnd, (HMENU)IDC_EDIT, g_hInst, NULL);
    /* Sous NT, leve la limite de 32 Ko ; sous 9x le noyau plafonne a ~64 Ko. */
    SendMessage(g_hEdit, EM_SETLIMITTEXT, 0, 0);
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
    char *text;
    BOOL modified;
    HMENU menu = GetMenu(g_hWnd);

    len = GetWindowTextLength(g_hEdit);
    text = (char *)HeapAlloc(GetProcessHeap(), 0, len + 1);
    if (!text) { error_box("M\xE9moire insuffisante."); return; }
    GetWindowText(g_hEdit, text, len + 1);
    modified = (BOOL)SendMessage(g_hEdit, EM_GETMODIFY, 0, 0);

    DestroyWindow(g_hEdit);
    g_wrap = !g_wrap;
    create_edit(text, modified);
    HeapFree(GetProcessHeap(), 0, text);

    CheckMenuItem(menu, IDM_WRAP, g_wrap ? MF_CHECKED : MF_UNCHECKED);
}

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(),
          edit = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();

    AppendMenu(file, MF_STRING, IDM_NEW,    "&Nouveau\tCtrl+N");
    AppendMenu(file, MF_STRING, IDM_OPEN,   "&Ouvrir...\tCtrl+O");
    AppendMenu(file, MF_STRING, IDM_SAVE,   "&Enregistrer\tCtrl+S");
    AppendMenu(file, MF_STRING, IDM_SAVEAS, "Enregistrer &sous...");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_EXIT,   "&Quitter");

    AppendMenu(edit, MF_STRING, IDM_UNDO,   "&Annuler\tCtrl+Z");
    AppendMenu(edit, MF_SEPARATOR, 0, NULL);
    AppendMenu(edit, MF_STRING, IDM_CUT,    "Co&uper\tCtrl+X");
    AppendMenu(edit, MF_STRING, IDM_COPY,   "&Copier\tCtrl+C");
    AppendMenu(edit, MF_STRING, IDM_PASTE,  "C&oller\tCtrl+V");
    AppendMenu(edit, MF_SEPARATOR, 0, NULL);
    AppendMenu(edit, MF_STRING, IDM_SELALL, "&Tout s\xE9lectionner\tCtrl+A");

    AppendMenu(view, MF_STRING | MF_CHECKED, IDM_WRAP, "&Retour \xE0 la ligne");

    AppendMenu(help, MF_STRING, IDM_ABOUT,  "\xC0 &propos");

    AppendMenu(bar, MF_POPUP, (UINT_PTR)file, "&Fichier");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)edit, "&\xC9" "dition");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)view, "&Affichage");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)help, "&?");
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
    /* Arial est livree avec toutes les versions de Windows depuis 3.1. */
    return CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hWnd = hwnd;
        g_hFont = create_font();
        create_edit("", FALSE);
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
                error_box("Limite de taille du texte atteinte sur ce syst\xE8me.");
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
            MessageBox(hwnd, APP_NAME "\n\n" APP_SLOGAN "\n\n" APP_LEGAL,
                       "\xC0 propos", MB_OK | MB_ICONINFORMATION);
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
static void first_arg(const char *cmd, char *out)
{
    int n = 0;
    char end = ' ';
    if (*cmd == '"') { ++cmd; while (*cmd && *cmd != '"') ++cmd; if (*cmd) ++cmd; }
    else while (*cmd && *cmd != ' ') ++cmd;
    while (*cmd == ' ') ++cmd;
    if (*cmd == '"') { end = '"'; ++cmd; }
    while (*cmd && *cmd != end && n < MAX_PATH - 1) out[n++] = *cmd++;
    out[n] = '\0';
}

static int run(void)
{
    WNDCLASS wc;
    MSG msg;
    HACCEL accels;
    char arg[MAX_PATH];

    g_hInst = GetModuleHandle(NULL);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = "GastinelWriteLegacy";
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
