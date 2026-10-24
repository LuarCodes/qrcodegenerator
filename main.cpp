// QR Clip - native Windows app (C++ / Win32)
// Type or paste a link/text, or open a .txt file -> QR code -> clipboard.
// Enter = copy, Shift+Enter = new line, Ctrl+O = open file, Ctrl+S = save PNG, Ctrl+A = select all.
// QR encoding: Nayuki QR Code generator (MIT).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include "qrcodegen.hpp"
using qrcodegen::QrCode;

// ---------- theme ----------
static const COLORREF C_BG = RGB(15, 17, 21), C_CARD = RGB(24, 27, 34), C_INPUT = RGB(17, 20, 26),
    C_BORDER = RGB(42, 47, 58), C_TEXT = RGB(230, 232, 236), C_MUTED = RGB(138, 143, 152),
    C_ACCENT = RGB(124, 92, 255), C_ACCENT_H = RGB(142, 115, 255), C_ACCENT_P = RGB(100, 71, 240),
    C_BTN = RGB(37, 42, 53), C_BTN_H = RGB(47, 53, 66), C_BTN_P = RGB(56, 63, 78),
    C_OK = RGB(61, 220, 132), C_WARN = RGB(255, 176, 32), C_ERR = RGB(255, 107, 107),
    C_EMPTY = RGB(30, 33, 41);

enum { ID_EDIT = 100, ID_PASTE, ID_OPEN, ID_CLEAR, ID_COPY, ID_SAVE };
enum { TIMER_RENDER = 1, TIMER_RESET = 2 };
static const int SCALE = 10, BORDER = 4;

static HINSTANCE gInst;
static HWND gWnd, gEdit, gBtns[5];
static HFONT fTitle, fSub, fBody, fBtn, fBtnBold, fSmall, fEdit;
static HBRUSH brInput;
static int gDpi = 96;
static int S(int v) { return MulDiv(v, gDpi, 96); }

static bool gHasQr = false;
static std::vector<uint8_t> gMods;  // 1 = dark, includes quiet zone
static int gN = 0;
static size_t gChars = 0;
static bool gCopied = false;
static std::wstring gStatus = L"Ready  \x00B7  Enter = copy  \x00B7  Shift+Enter = new line";
static COLORREF gStatusColor = C_MUTED;

static double nowMs() {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
}
static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring decode(const char* p, int len, UINT cp, DWORD flags = 0) {
    if (len <= 0) return {};
    int n = MultiByteToWideChar(cp, flags, p, len, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, flags, p, len, &w[0], n);
    return w;
}
static std::wstring getEditText() {
    int n = GetWindowTextLengthW(gEdit);
    std::vector<wchar_t> buf(n + 1);
    GetWindowTextW(gEdit, buf.data(), n + 1);
    std::wstring w(buf.data());
    std::wstring out; out.reserve(w.size());
    for (wchar_t c : w) if (c != L'\r') out += c;  // CRLF -> LF
    size_t a = out.find_first_not_of(L" \t\n"), b = out.find_last_not_of(L" \t\n");
    return a == std::wstring::npos ? L"" : out.substr(a, b - a + 1);
}
static void setEditText(const std::wstring& t) {
    std::wstring w; w.reserve(t.size() + 64);
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == L'\n' && (i == 0 || t[i - 1] != L'\r')) w += L'\r';
        w += t[i];
    }
    SetWindowTextW(gEdit, w.c_str());
}
static void setStatus(const std::wstring& s, COLORREF c) {
    gStatus = s; gStatusColor = c; InvalidateRect(gWnd, nullptr, FALSE);
}

// ---------- QR ----------
static bool render() {
    std::wstring w = getEditText();
    gChars = w.size();
    if (w.empty()) { gHasQr = false; InvalidateRect(gWnd, nullptr, FALSE); return false; }
    double t = nowMs();
    try {
        std::string u = toUtf8(w);
        QrCode qr = QrCode::encodeText(u.c_str(), QrCode::Ecc::MEDIUM);
        int sz = qr.getSize();
        gN = sz + 2 * BORDER;
        gMods.assign(gN * gN, 0);
        for (int y = 0; y < gN; y++)
            for (int x = 0; x < gN; x++)
                gMods[y * gN + x] = qr.getModule(x - BORDER, y - BORDER) ? 1 : 0;
        gHasQr = true;
        wchar_t msg[160];
        swprintf(msg, 160, L"Preview ready in %.1f ms  \x00B7  Enter = copy", nowMs() - t);
        setStatus(msg, C_MUTED);
        return true;
    } catch (const std::length_error&) {
        gHasQr = false;
        wchar_t msg[160];
        swprintf(msg, 160, L"Too long for one QR code (%u chars, max \x2248 2900)", (unsigned)gChars);
        setStatus(msg, C_ERR);
        return false;
    }
}

static std::vector<uint8_t> grayPixels(int& w) {
    w = gN * SCALE;
    std::vector<uint8_t> px(w * w);
    for (int y = 0; y < w; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = gMods[(y / SCALE) * gN + x / SCALE] ? 0 : 255;
    return px;
}

static std::vector<uint8_t> makeDib(const std::vector<uint8_t>& px, int w) {
    int stride = (w * 3 + 3) & ~3;
    std::vector<uint8_t> d(sizeof(BITMAPINFOHEADER) + stride * w, 0);
    auto* bi = (BITMAPINFOHEADER*)d.data();
    bi->biSize = sizeof(BITMAPINFOHEADER); bi->biWidth = w; bi->biHeight = w;
    bi->biPlanes = 1; bi->biBitCount = 24; bi->biCompression = BI_RGB; bi->biSizeImage = stride * w;
    uint8_t* bits = d.data() + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < w; y++) {
        uint8_t* row = bits + (w - 1 - y) * stride;  // bottom-up
        for (int x = 0; x < w; x++) { uint8_t v = px[y * w + x]; row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = v; }
    }
    return d;
}

static uint32_t crcTable[256];
static void initCrc() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, (uint32_t)data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = start; i < out.size(); i++) c = crcTable[(c ^ out[i]) & 0xFF] ^ (c >> 8);
    put32(out, c ^ 0xFFFFFFFFu);
}
// 1-bit grayscale PNG, zlib "stored" blocks (tiny + instant)
static std::vector<uint8_t> makePng(const std::vector<uint8_t>& px, int w) {
    int rowBytes = (w + 7) / 8;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * w);
    for (int y = 0; y < w; y++) {
        raw.push_back(0);
        for (int b = 0; b < rowBytes; b++) {
            uint8_t v = 0;
            for (int k = 0; k < 8; k++) {
                int x = b * 8 + k;
                if (x < w && px[y * w + x]) v |= (uint8_t)(0x80 >> k);
            }
            raw.push_back(v);
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        size_t n = raw.size() - pos; if (n > 65535) n = 65535;
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8);
        z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put32(ihdr, w); put32(ihdr, w);
    ihdr.push_back(1); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

static bool putClip(UINT fmt, const std::vector<uint8_t>& data) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!h) return false;
    memcpy(GlobalLock(h), data.data(), data.size());
    GlobalUnlock(h);
    if (!SetClipboardData(fmt, h)) { GlobalFree(h); return false; }
    return true;
}
static bool copyToClipboard() {
    int w; auto px = grayPixels(w);
    auto dib = makeDib(px, w);
    auto png = makePng(px, w);
    bool open = false;
    for (int i = 0; i < 20 && !(open = OpenClipboard(gWnd)); i++) Sleep(10);
    if (!open) return false;
    EmptyClipboard();
    bool ok = putClip(CF_DIB, dib);
    putClip(RegisterClipboardFormatW(L"PNG"), png);  // for browsers / Discord / etc.
    CloseClipboard();
    return ok;
}

// ---------- actions ----------
static void doCopy() {
    double t = nowMs();
    if (!render()) {
        if (getEditText().empty()) setStatus(L"Nothing to encode yet", C_WARN);
        return;
    }
    if (!copyToClipboard()) { setStatus(L"Copy failed: clipboard is busy, try again", C_ERR); return; }
    wchar_t msg[200];
    swprintf(msg, 200, L"\x2713 Copied to clipboard in %.1f ms \x2014 paste it anywhere with Ctrl+V", nowMs() - t);
    setStatus(msg, C_OK);
    gCopied = true;
    InvalidateRect(gBtns[3], nullptr, FALSE);
    SetTimer(gWnd, TIMER_RESET, 1200, nullptr);
}
static void doPaste() {
    std::wstring t;
    if (OpenClipboard(gWnd)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* p = (const wchar_t*)GlobalLock(h)) { t = p; GlobalUnlock(h); }
        }
        CloseClipboard();
    }
    if (t.empty()) { setStatus(L"Clipboard has no text", C_WARN); return; }
    setEditText(t);
    doCopy();
}
static void doOpen() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = gWnd;
    of.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    of.lpstrFile = path; of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&of)) return;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { setStatus(L"Could not open file", C_ERR); return; }
    DWORD size = GetFileSize(f, nullptr), got = 0;
    if (size > 50 * 1024 * 1024) { CloseHandle(f); setStatus(L"File is too big", C_ERR); return; }
    std::string data(size, '\0');
    if (size) ReadFile(f, &data[0], size, &got, nullptr);
    CloseHandle(f);
    data.resize(got);
    std::wstring t;
    const unsigned char* u = (const unsigned char*)data.data();
    if (got >= 2 && u[0] == 0xFF && u[1] == 0xFE) t.assign((const wchar_t*)(data.data() + 2), (got - 2) / 2);
    else if (got >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) t = decode(data.data() + 3, got - 3, CP_UTF8);
    else {
        t = decode(data.data(), got, CP_UTF8, MB_ERR_INVALID_CHARS);
        if (t.empty() && got) t = decode(data.data(), got, CP_ACP);
    }
    setEditText(t);
    doCopy();
}
static void doSave() {
    if (!render()) { setStatus(L"Nothing to save yet", C_WARN); return; }
    wchar_t path[MAX_PATH] = L"qrcode.png";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = gWnd;
    of.lpstrFilter = L"PNG image (*.png)\0*.png\0";
    of.lpstrFile = path; of.nMaxFile = MAX_PATH; of.lpstrDefExt = L"png";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&of)) return;
    int w; auto png = makePng(grayPixels(w), w);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD wr = 0;
    bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, png.data(), (DWORD)png.size(), &wr, nullptr);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    std::wstring name = path; size_t s = name.find_last_of(L"\\/");
    if (s != std::wstring::npos) name = name.substr(s + 1);
    setStatus(ok ? L"Saved " + name : L"Could not save file", ok ? C_OK : C_ERR);
}
static void doClear() {
    SetWindowTextW(gEdit, L"");
    render();
    setStatus(L"Cleared", C_MUTED);
    SetFocus(gEdit);
}

// ---------- drawing ----------
static void fillRound(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}
static void text(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), -1, &r, fmt | DT_NOPREFIX);
}
static RECT R(int x, int y, int w, int h) { return RECT{S(x), S(y), S(x + w), S(y + h)}; }

static void paint(HDC dc, RECT cl) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &cl, bg); DeleteObject(bg);

    text(dc, L"QR Clip", R(24, 16, 200, 40), fTitle, C_TEXT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    text(dc, L"link  \x00B7  text  \x00B7  .txt   \x2192   QR   \x2192   clipboard", R(140, 20, 500, 36), fSub, C_MUTED, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // left card
    fillRound(dc, R(24, 70, 500, 430), S(20), C_CARD, C_CARD);
    text(dc, L"Paste a link, type text, or open a file", R(42, 82, 460, 22), fBody, C_MUTED, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    fillRound(dc, R(42, 110, 464, 312), S(14), C_INPUT, C_BORDER);
    wchar_t cnt[64]; swprintf(cnt, 64, L"%u chars", (unsigned)gChars);
    text(dc, cnt, R(42, 424, 462, 20), fSmall, C_MUTED, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    // right card
    fillRound(dc, R(540, 70, 336, 430), S(20), C_CARD, C_CARD);
    RECT pv = R(560, 88, 296, 296);
    if (!gHasQr) {
        fillRound(dc, pv, S(14), C_EMPTY, C_EMPTY);
        text(dc, L"Your QR code appears here", pv, fBody, C_MUTED, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    } else {
        fillRound(dc, pv, S(14), RGB(255, 255, 255), RGB(255, 255, 255));
        int avail = pv.right - pv.left - S(8);
        int m = avail / gN; if (m < 1) m = 1;
        int sz = m * gN;
        int ox = pv.left + (pv.right - pv.left - sz) / 2, oy = pv.top + (pv.bottom - pv.top - sz) / 2;
        std::vector<uint32_t> bits(gN * gN);
        for (int i = 0; i < gN * gN; i++) bits[i] = gMods[i] ? 0x000000 : 0xFFFFFF;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = gN; bi.bmiHeader.biHeight = -gN;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, ox, oy, sz, sz, 0, 0, gN, gN, bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    }
    text(dc, gStatus, R(26, 508, 850, 24), fSmall, gStatusColor, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void drawButton(const DRAWITEMSTRUCT* di) {
    HDC dc = di->hDC; RECT r = di->rcItem;
    bool primary = di->CtlID == ID_COPY;
    bool pressed = di->itemState & ODS_SELECTED;
    bool hover = GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA) != 0;
    HBRUSH bg = CreateSolidBrush(C_CARD); FillRect(dc, &r, bg); DeleteObject(bg);
    COLORREF c = primary ? (pressed ? C_ACCENT_P : hover ? C_ACCENT_H : C_ACCENT)
                         : (pressed ? C_BTN_P : hover ? C_BTN_H : C_BTN);
    if (primary && gCopied) c = RGB(34, 160, 95);
    fillRound(dc, r, S(12), c, c);
    wchar_t label[64]; GetWindowTextW(di->hwndItem, label, 64);
    std::wstring s = (primary && gCopied) ? L"\x2713  Copied!" : label;
    text(dc, s, r, primary ? fBtnBold : fBtn, RGB(255, 255, 255), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

// ---------- subclasses ----------
static LRESULT CALLBACK btnProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_MOUSEMOVE && !GetWindowLongPtrW(h, GWLP_USERDATA)) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 1);
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t);
        InvalidateRect(h, nullptr, FALSE);
    } else if (m == WM_MOUSELEAVE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        InvalidateRect(h, nullptr, FALSE);
    } else if (m == WM_SETCURSOR) {
        SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE;
    }
    return DefSubclassProc(h, m, w, l);
}
static LRESULT CALLBACK editProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    bool shift = GetKeyState(VK_SHIFT) & 0x8000;
    if (m == WM_KEYDOWN && w == VK_RETURN && !shift) { doCopy(); return 0; }
    if (m == WM_CHAR && w == L'\r' && !shift) return 0;
    if (m == WM_CHAR && w == 1) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }  // Ctrl+A
    return DefSubclassProc(h, m, w, l);
}

// ---------- window ----------
static HFONT mkFont(int size, int weight, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(-S(size), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        gWnd = h;
        fTitle = mkFont(26, FW_BOLD); fSub = mkFont(14, FW_NORMAL); fBody = mkFont(14, FW_NORMAL);
        fBtn = mkFont(14, FW_SEMIBOLD); fBtnBold = mkFont(15, FW_BOLD); fSmall = mkFont(13, FW_NORMAL);
        fEdit = mkFont(15, FW_NORMAL);
        brInput = CreateSolidBrush(C_INPUT);

        gEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                S(54), S(120), S(444), S(292), h, (HMENU)ID_EDIT, gInst, nullptr);
        SendMessageW(gEdit, WM_SETFONT, (WPARAM)fEdit, TRUE);
        SendMessageW(gEdit, EM_SETLIMITTEXT, 10 * 1024 * 1024, 0);
        SetWindowTheme(gEdit, L"DarkMode_Explorer", nullptr);
        SetWindowSubclass(gEdit, editProc, 1, 0);

        struct { int id; const wchar_t* t; RECT r; } bs[] = {
            {ID_PASTE, L"Paste", R(42, 450, 148, 38)},
            {ID_OPEN, L"Open file", R(200, 450, 148, 38)},
            {ID_CLEAR, L"Clear", R(358, 450, 148, 38)},
            {ID_COPY, L"Copy QR to clipboard", R(560, 398, 296, 44)},
            {ID_SAVE, L"Save as PNG", R(560, 452, 296, 36)},
        };
        for (int i = 0; i < 5; i++) {
            gBtns[i] = CreateWindowExW(0, L"BUTTON", bs[i].t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       bs[i].r.left, bs[i].r.top, bs[i].r.right - bs[i].r.left, bs[i].r.bottom - bs[i].r.top,
                                       h, (HMENU)(INT_PTR)bs[i].id, gInst, nullptr);
            SetWindowSubclass(gBtns[i], btnProc, 1, 0);
        }
        BOOL dark = TRUE;
        DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));  // dark title bar (Win10 20H1+/Win11)
        DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));  // older Win10 builds
        return 0;
    }
    case WM_SHOWWINDOW: SetFocus(gEdit); break;
    case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE) SetFocus(gEdit); return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_EDIT: if (HIWORD(w) == EN_CHANGE) SetTimer(h, TIMER_RENDER, 60, nullptr); break;
        case ID_PASTE: doPaste(); break;
        case ID_OPEN: doOpen(); break;
        case ID_CLEAR: doClear(); break;
        case ID_COPY: doCopy(); break;
        case ID_SAVE: doSave(); break;
        }
        if (LOWORD(w) != ID_EDIT && LOWORD(w) != ID_CLEAR) SetFocus(gEdit);
        return 0;
    case WM_TIMER:
        KillTimer(h, w);
        if (w == TIMER_RENDER) render();
        else if (w == TIMER_RESET) { gCopied = false; InvalidateRect(gBtns[3], nullptr, FALSE); }
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, C_TEXT); SetBkColor((HDC)w, C_INPUT);
        return (LRESULT)brInput;
    case WM_DRAWITEM: drawButton((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT cl; GetClientRect(h, &cl);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cl.right, cl.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        paint(mem, cl);
        BitBlt(dc, 0, 0, cl.right, cl.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    gInst = inst;
    SetProcessDPIAware();
    HDC sdc = GetDC(nullptr); gDpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc);
    initCrc();
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = wndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"QRClipWnd";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = {0, 0, S(900), S(540)};
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"QR Clip", style, x, y, ww, wh, nullptr, nullptr, inst, nullptr);
    ShowWindow(h, show); UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (msg.wParam == 'O') { doOpen(); continue; }
            if (msg.wParam == 'S') { doSave(); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
