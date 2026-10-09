/*
 * Child-window scaling for Gangsters' native menu controls.
 *
 * Background: the game's menus use real Win32 child controls (owner-drawn BUTTONs, STATICs) positioned in *game*
 * coordinates (e.g. 640x480) on top of its DirectDraw picture. The game paints the button art (the metal bars)
 * onto its DirectDraw surface and only draws the label text on the control itself, relying on exclusive
 * fullscreen mode where the controls were effectively transparent. cnc-ddraw upscales the picture, so:
 *   1. controls must be moved/resized into the scaled, letterboxed area (WH_CBT hook + SetWindowPos hook);
 *   2. the game derives the bar position from GetWindowRect(button), so that call must report game-space
 *      coordinates (call-site patch, see patches.c);
 *   3. the controls must not cover the art with a white face: each owner-drawn button becomes a per-pixel-alpha
 *      layered window that we render ourselves (label text only, ~transparent elsewhere but still hit-testable).
 * Layered child windows need the Windows 8+ manifest (payload/gangsters.exe.manifest).
 */
#include <windows.h>
#include <string.h>
#include "mci_music.h"

BOOL (WINAPI *g_next_SetWindowPos)(HWND, HWND, int, int, int, int, UINT);
BOOL (WINAPI *g_next_GetClientRect)(HWND, LPRECT);
BOOL (WINAPI *g_next_GetWindowRect)(HWND, LPRECT);

typedef struct { double sx, sy; int ox, oy; } Map;

static int g_enabled = 1;
static int g_maintain_ar = 1;
static int g_render_labels = 1;
static int g_verbose;
static HHOOK g_cbt, g_ret;

/* genuine user32 entry points, used to break recursion if another hook (cnc-ddraw) chains back to us */
static BOOL (WINAPI *real_SetWindowPos)(HWND, HWND, int, int, int, int, UINT);
static BOOL (WINAPI *real_GetClientRect)(HWND, LPRECT);
static BOOL (WINAPI *real_GetWindowRect)(HWND, LPRECT);
static int g_in_swp, g_in_gcr, g_in_gwr;

void window_fix_configure(int enabled, int maintain_ar, int verbose) {
    g_enabled = enabled; g_maintain_ar = maintain_ar; g_verbose = verbose;
}
void window_fix_set_render_labels(int on) { g_render_labels = on; }

/* ---------- geometry ---------- */

/* Game-space -> client-space mapping for the top-level window containing `w`. Returns 0 if identity/unknown. */
static int get_map(HWND w, Map *m) {
    HWND top = GetAncestor(w, GA_ROOT);
    if (!top) return 0;
    WINDOWINFO wi = {sizeof wi};
    if (!GetWindowInfo(top, &wi)) return 0;
    int rw = wi.rcClient.right - wi.rcClient.left, rh = wi.rcClient.bottom - wi.rcClient.top;
    RECT lr;
    if (!real_GetClientRect(top, &lr)) return 0;   /* cnc-ddraw reports the game's logical size here */
    int lw = lr.right, lh = lr.bottom;
    if (lw <= 0 || lh <= 0 || rw <= 0 || rh <= 0) return 0;
    if (lw == rw && lh == rh) return 0;
    m->sx = (double)rw / lw;
    m->sy = (double)rh / lh;
    if (g_maintain_ar) m->sx = m->sy = (m->sx < m->sy ? m->sx : m->sy);
    m->ox = (int)((rw - lw * m->sx) / 2);
    m->oy = (int)((rh - lh * m->sy) / 2);
    return 1;
}

/* Only direct children of the top-level window get the letterbox offset; deeper descendants (controls inside a
 * dialog) are positioned relative to their already-scaled parent. */
static int get_child_map(HWND parent, Map *m) {
    if (!get_map(parent, m)) return 0;
    if (GetAncestor(parent, GA_ROOT) != parent) m->ox = m->oy = 0;
    return 1;
}

static void map_rect(const Map *m, int *x, int *y, int *w, int *h) {
    if (x) *x = m->ox + (int)(*x * m->sx + 0.5);
    if (y) *y = m->oy + (int)(*y * m->sy + 0.5);
    if (w) *w = (int)(*w * m->sx + 0.5);
    if (h) *h = (int)(*h * m->sy + 0.5);
}

/* ---------- label rendering ---------- */

static int is_owner_draw_button(HWND h, const char *cls) {
    return !lstrcmpiA(cls, "BUTTON") && (GetWindowLongA(h, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW;
}

/*
 * Overlays. Each owner-drawn button / text static gets a top-level, per-pixel-alpha popup window owned by the game
 * window and placed exactly over it. Top-level layered windows work on every Windows version without a manifest
 * (layered *child* windows need one, and Windows only honours a newly added manifest after the exe's timestamp
 * changes, which a plain file copy cannot do). The popup carries the label and forwards mouse input to the real
 * control; the real control is given an empty window region so its own white face never shows. Its rectangle,
 * visibility and enabled state - everything the game reads - are unchanged.
 */
typedef struct { HWND child, popup; int is_static; RECT rect; int shown; } Overlay;
static Overlay g_ov[160];
static ATOM g_ov_class;

static Overlay *find_overlay(HWND child) {
    for (int i = 0; i < 160; i++) if (g_ov[i].child == child) return &g_ov[i];
    return NULL;
}

static LRESULT CALLBACK overlay_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    HWND child = (HWND)(LONG_PTR)GetWindowLongA(h, GWL_USERDATA);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
    }   /* fall through */
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MOUSELEAVE:
        /* same size and origin as the control, so client coordinates carry over unchanged */
        if (child && IsWindow(child)) SendMessageA(child, msg, wp, lp);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static Overlay *make_overlay(HWND child, int is_static) {
    Overlay *ov = find_overlay(child);
    if (ov) return ov;
    if (!g_ov_class) {
        WNDCLASSA wc = {0};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = overlay_proc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
        wc.lpszClassName = "GocPatchOverlay";
        g_ov_class = RegisterClassA(&wc);
        if (!g_ov_class) return NULL;
    }
    for (int i = 0; i < 160; i++) if (!g_ov[i].child) { ov = &g_ov[i]; break; }
    if (!ov) return NULL;
    HWND owner = GetAncestor(child, GA_ROOT);
    RECT r;
    real_GetWindowRect(child, &r);
    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (is_static ? WS_EX_TRANSPARENT : 0);
    HWND popup = CreateWindowExA(ex, "GocPatchOverlay", "", WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                 owner, NULL, GetModuleHandleA(NULL), NULL);
    if (!popup) return NULL;
    SetWindowLongA(popup, GWL_USERDATA, (LONG)(LONG_PTR)child);
    ov->child = child; ov->popup = popup; ov->is_static = is_static; ov->rect = r; ov->shown = 0;
    int rgn_ok = SetWindowRgn(child, CreateRectRgn(0, 0, 0, 0), FALSE);   /* the real control is invisible and takes no input */
    if (g_verbose) {
        HDC probe = GetDC(child);
        RECT cb;
        int kind = GetClipBox(probe, &cb);
        ReleaseDC(child, probe);
        shim_log("SetWindowRgn(%p) -> %d; clip box kind=%d (1=empty)", (void *)child, rgn_ok, kind);
    }
    if (g_verbose) shim_log("overlay %p created for %s %p", (void *)popup, is_static ? "static" : "button", (void *)child);
    return ov;
}

/* Keep each popup glued to its control (position, size, visibility) and drop popups of destroyed controls. */
static void overlay_sync_needs_render(Overlay *ov);
static void sync_overlays(void) {
    for (int i = 0; i < 160; i++) {
        Overlay *ov = &g_ov[i];
        if (!ov->child) continue;
        if (!IsWindow(ov->child)) {
            DestroyWindow(ov->popup);
            memset(ov, 0, sizeof *ov);
            continue;
        }
        HWND root = GetAncestor(ov->child, GA_ROOT);
        int vis = IsWindowVisible(ov->child) && root && IsWindowVisible(root) && !IsIconic(root);
        RECT r;
        real_GetWindowRect(ov->child, &r);
        if (memcmp(&r, &ov->rect, sizeof r)) {
            ov->rect = r;
            real_SetWindowPos(ov->popup, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_NOZORDER);
            overlay_sync_needs_render(ov);
        }
        if (vis != ov->shown) {
            ShowWindow(ov->popup, vis ? SW_SHOWNOACTIVATE : SW_HIDE);
            ov->shown = vis;
        }
    }
}

typedef struct { HWND h; unsigned key; } LabelState;
static LabelState g_labels[64];

static unsigned label_key(const char *txt, int disabled, int w, int hgt) {
    unsigned k = 2166136261u ^ (disabled ? 0x9e3779b1u : 0) ^ (unsigned)(w * 7919 + hgt);
    for (; *txt; txt++) k = (k ^ (unsigned char)*txt) * 16777619u;
    return k ? k : 1;
}

/* Renders the control's label into a 32-bit premultiplied-alpha DIB and applies it with UpdateLayeredWindow. */
static void render_label(HWND h) {
    RECT rc;
    if (!real_GetWindowRect(h, &rc)) return;
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    Overlay *ov = find_overlay(h);
    if (w <= 0 || hgt <= 0 || !ov) return;
    char txt[128] = "";
    GetWindowTextA(h, txt, sizeof txt);
    int disabled = !IsWindowEnabled(h);
    unsigned key = label_key(txt, disabled, w, hgt);

    LabelState *slot = NULL;
    for (int i = 0; i < 64; i++) {
        if (g_labels[i].h == h) { slot = &g_labels[i]; break; }
        if (!slot && !g_labels[i].h) slot = &g_labels[i];
    }
    if (slot && slot->h == h && slot->key == key) return;   /* nothing changed */
    if (slot) { slot->h = h; slot->key = key; }

    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -hgt, 1, 32, BI_RGB, 0, 0, 0, 0, 0}};
    unsigned char *bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        memset(bits, 0, (size_t)w * hgt * 4);
        /* white text on black; the green channel becomes the glyph coverage */
        HFONT font = CreateFontA(-(int)(hgt * 0.50), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
        HGDIOBJ oldf = SelectObject(mem, font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 255, 255));
        RECT tr = {0, 0, w, hgt};
        DrawTextA(mem, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mem, oldf);
        DeleteObject(font);
        GdiFlush();

        const int cr = disabled ? 78 : 24, cg = disabled ? 78 : 24, cb = disabled ? 78 : 24;
        unsigned char *p = bits;
        for (int i = 0; i < w * hgt; i++, p += 4) {
            int a = p[1];                 /* coverage */
            if (a < 1) a = 1;             /* keep every pixel hit-testable (alpha 0 is click-through) */
            p[0] = (unsigned char)(cb * a / 255);
            p[1] = (unsigned char)(cg * a / 255);
            p[2] = (unsigned char)(cr * a / 255);
            p[3] = (unsigned char)a;
        }
        POINT src = {0, 0}, dst = {rc.left, rc.top};
        SIZE sz = {w, hgt};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        BOOL ok = UpdateLayeredWindow(ov->popup, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
        if (ok && !ov->shown && IsWindowVisible(h)) { ShowWindow(ov->popup, SW_SHOWNOACTIVATE); ov->shown = 1; }
        if (g_verbose) shim_log("render_label %p '%s' %dx%d disabled=%d ok=%d err=%lu", (void *)h, txt, w, hgt, disabled, ok,
                                ok ? 0 : GetLastError());
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

/*
 * Layered (UpdateLayeredWindow) windows never get WM_PAINT, but the game's painter - which draws the button art
 * onto the DirectDraw surface - only runs in response to the WM_DRAWITEM the button's paint produces. So we
 * emulate the repaint: on a light timer, send each owner-drawn button the WM_DRAWITEM the system would have sent.
 * The painter alternates between "invalidate" and "draw" on successive calls (global toggle), hence two sends.
 */
static HWND g_game_root;

static BOOL CALLBACK repaint_child(HWND h, LPARAM lp) {
    char cls[32] = "";
    HWND root = (HWND)lp;
    if (GetParent(h) != root || !IsWindowVisible(h)) return TRUE;
    GetClassNameA(h, cls, sizeof cls);
    if (!is_owner_draw_button(h, cls)) return TRUE;
    RECT rc;
    real_GetClientRect(h, &rc);
    Map m;
    int dw = rc.right, dh = rc.bottom;
    if (get_child_map(root, &m)) { dw = (int)(dw / m.sx + 0.5); dh = (int)(dh / m.sy + 0.5); }
    UINT state = 0;
    if (!IsWindowEnabled(h)) state |= ODS_DISABLED;
    if (GetFocus() == h) state |= ODS_FOCUS;
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        POINT p; GetCursorPos(&p);
        RECT wr; real_GetWindowRect(h, &wr);
        if (PtInRect(&wr, p)) state |= ODS_SELECTED;
    }
    HDC dc = CreateCompatibleDC(NULL);   /* throwaway DC: the painter's own (tiny) label text goes nowhere; we draw labels ourselves */
    DRAWITEMSTRUCT d = {ODT_BUTTON, (UINT)GetDlgCtrlID(h), 0, ODA_DRAWENTIRE, state, h, dc, {0, 0, dw, dh}, 0};
    for (int i = 0; i < 2; i++) SendMessageA(root, WM_DRAWITEM, d.CtlID, (LPARAM)&d);
    DeleteDC(dc);
    return TRUE;
}

static void flush_pending_statics(void);

static VOID CALLBACK repaint_tick(HWND unused, UINT msg, UINT_PTR id, DWORD now) {
    (void)unused; (void)msg; (void)id; (void)now;
    static int busy;
    if (busy) return;
    busy = 1;
    flush_pending_statics();
    sync_overlays();
    if (!g_game_root || !IsWindow(g_game_root)) { busy = 0; return; }
    EnumChildWindows(g_game_root, repaint_child, (LPARAM)g_game_root);
    busy = 0;
}

/*
 * Regular text controls (list boxes, edit boxes, static text, check boxes ...): the game sets up their fonts at
 * design size, so after scaling the control the text would stay tiny. Give them the same font scaled by the
 * UI factor. The game may call WM_SETFONT itself later; we re-apply after it, and remember the fonts we created
 * so we never scale a font twice.
 */
static HFONT g_fonts[48];
static int g_nfonts;
static int g_in_setfont;

static int is_our_font(HFONT f) {
    for (int i = 0; i < g_nfonts; i++) if (g_fonts[i] == f) return 1;
    return 0;
}

static HFONT scaled_font_from(HFONT cur, double s) {
    LOGFONTA lf;
    if (!cur) cur = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!GetObjectA(cur, sizeof lf, &lf)) return NULL;
    if (!lf.lfHeight) lf.lfHeight = -13;
    lf.lfHeight = (LONG)(lf.lfHeight * s - (lf.lfHeight < 0 ? 0.5 : -0.5));
    lf.lfWidth = 0;
    if (lf.lfQuality == DEFAULT_QUALITY) lf.lfQuality = ANTIALIASED_QUALITY;
    for (int i = 0; i < g_nfonts; i++) {   /* reuse an identical font */
        LOGFONTA o;
        if (GetObjectA(g_fonts[i], sizeof o, &o) && !memcmp(&o, &lf, sizeof lf)) return g_fonts[i];
    }
    HFONT f = CreateFontIndirectA(&lf);
    if (f && g_nfonts < 48) g_fonts[g_nfonts++] = f;
    return f;
}

static int is_scalable_text_class(const char *cls) {
    return !lstrcmpiA(cls, "STATIC") || !lstrcmpiA(cls, "EDIT") || !lstrcmpiA(cls, "LISTBOX") ||
           !lstrcmpiA(cls, "COMBOBOX") || !lstrcmpiA(cls, "BUTTON") || !lstrcmpiA(cls, "SCROLLBAR");
}

typedef struct { HWND h; HFONT scaled; } FontMap;
static FontMap g_fontmap[256];

static HFONT mapped_font(HWND h) {
    for (int i = 0; i < 256; i++) if (g_fontmap[i].h == h) return g_fontmap[i].scaled;
    return NULL;
}

/* `src` = the font the game just assigned (WM_SETFONT wParam), or NULL to query the control. */
static void scale_control_font(HWND h, HFONT src, int from_setfont) {
    char cls[32] = "";
    Map m;
    HWND parent = GetParent(h);
    if (g_in_setfont || !parent || !(GetWindowLongA(h, GWL_STYLE) & WS_CHILD)) return;
    GetClassNameA(h, cls, sizeof cls);
    if (!is_scalable_text_class(cls) || is_owner_draw_button(h, cls)) return;
    if (!get_child_map(parent, &m)) return;
    HFONT cur = from_setfont ? src : (HFONT)SendMessageA(h, WM_GETFONT, 0, 0);
    if (cur && is_our_font(cur)) return;
    HFONT f = scaled_font_from(cur, m.sy);
    if (!f) return;
    int slot = -1;
    for (int i = 0; i < 256; i++) {
        if (g_fontmap[i].h == h) { slot = i; break; }
        if (slot < 0 && !g_fontmap[i].h) slot = i;
    }
    if (slot >= 0) { g_fontmap[slot].h = h; g_fontmap[slot].scaled = f; }
    g_in_setfont = 1;
    SendMessageA(h, WM_SETFONT, (WPARAM)f, TRUE);
    g_in_setfont = 0;
    if (g_verbose) shim_log("scaled font of %s control %p (x%.2f)", cls, (void *)h, m.sy);
}

/*
 * Plain text statics (scenario descriptions, names, headings ...). The game relies on them painting transparently
 * (hollow brush from WM_CTLCOLORSTATIC) straight onto the screen. Their pixels do not survive in our composition, so
 * - like the buttons - they become per-pixel-alpha layered windows that we render: the game's own text colour (asked
 * of its WM_CTLCOLORSTATIC handler), the scaled font, alignment and word wrap, with a fully transparent background.
 */
static LabelState g_statics[256];
typedef struct { HWND h; COLORREF col; } StaticColour;
static StaticColour g_scolor[256];
static HWND g_pending[64];
static int g_npending;

/* Called after the game's WM_CTLCOLORSTATIC handler ran: remember the text colour it set and queue a render. */
static void note_static_colour(HWND parent, HDC dc, HWND ctl) {
    (void)parent;
    if (!ctl || !IsWindow(ctl)) return;
    COLORREF c = GetTextColor(dc);
    int slot = -1;
    for (int i = 0; i < 256; i++) {
        if (g_scolor[i].h == ctl) { slot = i; break; }
        if (slot < 0 && !g_scolor[i].h) slot = i;
    }
    if (slot < 0) return;
    int changed = g_scolor[slot].h != ctl || g_scolor[slot].col != c;
    g_scolor[slot].h = ctl; g_scolor[slot].col = c;
    if (changed && g_npending < 64) g_pending[g_npending++] = ctl;
}

static int is_text_static(HWND h, const char *cls) {
    if (lstrcmpiA(cls, "STATIC")) return 0;
    DWORD t = GetWindowLongA(h, GWL_STYLE) & SS_TYPEMASK;
    return t == SS_LEFT || t == SS_CENTER || t == SS_RIGHT || t == SS_LEFTNOWORDWRAP;
}

static void render_static(HWND h) {
    RECT rc;
    HWND parent = GetParent(h);
    if (!parent || !real_GetWindowRect(h, &rc)) return;
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    if (w <= 0 || hgt <= 0) return;
    /* the colour the game chose for this control, seen when it last painted it (see note_static_colour) */
    COLORREF col = 0;
    int have = 0;
    for (int i = 0; i < 256; i++) if (g_scolor[i].h == h) { col = g_scolor[i].col; have = 1; break; }
    if (!have) return;            /* not painted by the game yet: leave it to the system until we know its colour */
    col = ((col >> 24) == 1) ? RGB(255, 255, 255) : (col & 0xFFFFFF);   /* palette index: cannot be resolved here */
    Overlay *ov = make_overlay(h, 1);
    if (!ov) return;

    int len = GetWindowTextLengthA(h);
    char *txt = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)len + 2);
    if (!txt) return;
    GetWindowTextA(h, txt, len + 1);

    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);

    HFONT font = mapped_font(h);
    if (!font) font = (HFONT)SendMessageA(h, WM_GETFONT, 0, 0);
    unsigned key = label_key(txt, 0, w, hgt) ^ (unsigned)col * 2654435761u ^ (unsigned)(ULONG_PTR)font;
    LabelState *slot = NULL;
    for (int i = 0; i < 256; i++) {
        if (g_statics[i].h == h) { slot = &g_statics[i]; break; }
        if (!slot && !g_statics[i].h) slot = &g_statics[i];
    }
    if (slot && slot->h == h && slot->key == key) goto done;
    if (slot) { slot->h = h; slot->key = key; }

    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -hgt, 1, 32, BI_RGB, 0, 0, 0, 0, 0}};
    unsigned char *bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        memset(bits, 0, (size_t)w * hgt * 4);
        HGDIOBJ oldf = font ? SelectObject(mem, font) : NULL;
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 255, 255));
        DWORD style = GetWindowLongA(h, GWL_STYLE), t = style & SS_TYPEMASK;
        UINT fl = DT_NOPREFIX | DT_TOP | (t == SS_CENTER ? DT_CENTER : t == SS_RIGHT ? DT_RIGHT : DT_LEFT) |
                  (t == SS_LEFTNOWORDWRAP ? DT_SINGLELINE : DT_WORDBREAK);
        RECT tr = {0, 0, w, hgt};
        DrawTextA(mem, txt, -1, &tr, fl);
        if (oldf) SelectObject(mem, oldf);
        GdiFlush();
        const int cr = GetRValue(col), cg = GetGValue(col), cb = GetBValue(col);
        unsigned char *p = bits;
        for (int i = 0; i < w * hgt; i++, p += 4) {
            int a = p[1];                                  /* glyph coverage; 0 elsewhere = fully transparent */
            p[0] = (unsigned char)(cb * a / 255);
            p[1] = (unsigned char)(cg * a / 255);
            p[2] = (unsigned char)(cr * a / 255);
            p[3] = (unsigned char)a;
        }
        POINT src = {0, 0}, dst = {rc.left, rc.top};
        SIZE sz = {w, hgt};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        BOOL ok = UpdateLayeredWindow(ov->popup, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
        if (ok && !ov->shown && IsWindowVisible(h)) { ShowWindow(ov->popup, SW_SHOWNOACTIVATE); ov->shown = 1; }
        if (g_verbose) {
            LOGFONTA lf = {0};
            if (font) GetObjectA(font, sizeof lf, &lf);
            shim_log("render_static %p %dx%d colour=%06lx font=%p(h=%ld ours=%d '%s') ok=%d text='%.40s'", (void *)h, w, hgt,
                     (unsigned long)col, (void *)font, lf.lfHeight, is_our_font(font), lf.lfFaceName, ok, txt);
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
done:
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    HeapFree(GetProcessHeap(), 0, txt);
}

static void refresh_static(HWND h) {
    char cls[32] = "";
    Map m;
    HWND parent = GetParent(h);
    if (!parent || !(GetWindowLongA(h, GWL_STYLE) & WS_CHILD)) return;
    GetClassNameA(h, cls, sizeof cls);
    if (is_text_static(h, cls) && get_child_map(parent, &m)) render_static(h);
}

/* A popup was moved/resized: forget what it last showed so the label is drawn again at the new size. */
static void overlay_sync_needs_render(Overlay *ov) {
    for (int i = 0; i < 64; i++) if (g_labels[i].h == ov->child) g_labels[i].key = 0;
    for (int i = 0; i < 256; i++) if (g_statics[i].h == ov->child) g_statics[i].key = 0;
    if (ov->is_static) refresh_static(ov->child); else render_label(ov->child);
}

static void flush_pending_statics(void) {
    HWND todo[64];
    int n = g_npending;
    memcpy(todo, g_pending, sizeof(HWND) * n);
    g_npending = 0;
    for (int i = 0; i < n; i++) if (IsWindow(todo[i])) refresh_static(todo[i]);
}

/* ---------- hooks ---------- */

/* Runs before a window exists: rewrite the creation geometry of child windows. */
static LRESULT CALLBACK cbt_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND && g_enabled) {
        CBT_CREATEWNDA *cw = (CBT_CREATEWNDA *)lp;
        CREATESTRUCTA *cs = cw->lpcs;
        if ((cs->style & WS_CHILD) && cs->hwndParent) {
            Map m;
            if (get_child_map(cs->hwndParent, &m)) {
                if (GetAncestor(cs->hwndParent, GA_ROOT) == cs->hwndParent) g_game_root = cs->hwndParent;
                int x = cs->x, y = cs->y, w = cs->cx, h = cs->cy;
                map_rect(&m, &x, &y, &w, &h);
                if (g_verbose)
                    shim_log("CBT child %p class=%s style=%#lx (%d,%d %dx%d) -> (%d,%d %dx%d)", (void *)wp,
                             (ULONG_PTR)cs->lpszClass < 0x10000 ? "<atom>" : cs->lpszClass, (unsigned long)cs->style,
                             cs->x, cs->y, cs->cx, cs->cy, x, y, w, h);
                cs->x = x; cs->y = y; cs->cx = w; cs->cy = h;
            }
        }
    }
    return CallNextHookEx(g_cbt, code, wp, lp);
}

/* Runs after a message was handled: turn owner-drawn buttons into self-rendered layered windows. */
static LRESULT CALLBACK ret_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_enabled && g_render_labels) {
        const CWPRETSTRUCT *r = (const CWPRETSTRUCT *)lp;
        if (g_verbose && (r->message == WM_CTLCOLORSTATIC || r->message == WM_ERASEBKGND || r->message == WM_PAINT)) {
            char cls[32] = "";
            GetClassNameA(r->hwnd, cls, sizeof cls);
            static int n;
            if (n++ < 60)
                shim_log("msg %#x to %s %p -> %p (wParam=%p lParam=%p)", r->message, cls, (void *)r->hwnd, (void *)r->lResult,
                         (void *)r->wParam, (void *)r->lParam);
        }
        if (r->message == WM_CTLCOLORSTATIC) note_static_colour(r->hwnd, (HDC)r->wParam, (HWND)r->lParam);
        switch (r->message) {
        case WM_CREATE: {
            char cls[32] = "";
            HWND h = r->hwnd;
            GetClassNameA(h, cls, sizeof cls);
            Map m;
            if (GetParent(h) && is_owner_draw_button(h, cls) && get_child_map(GetParent(h), &m)) {
                if (make_overlay(h, 0)) render_label(h);
            } else {
                scale_control_font(h, NULL, 0);
                refresh_static(h);
            }
            break;
        }
        case WM_SETFONT:
            scale_control_font(r->hwnd, (HFONT)r->wParam, 1);
            refresh_static(r->hwnd);
            break;
        case WM_INITDIALOG:
            for (HWND c = GetWindow(r->hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) scale_control_font(c, NULL, 0);
            break;
        case WM_SETTEXT:
        case WM_ENABLE:
        case WM_SIZE:
        case WM_SHOWWINDOW:
        case WM_DRAWITEM:
        case WM_PAINT: {
            HWND h = r->message == WM_DRAWITEM ? ((const DRAWITEMSTRUCT *)r->lParam)->hwndItem : r->hwnd;
            if (h && r->message != WM_DRAWITEM && r->message != WM_PAINT) refresh_static(h);   /* text/size/visibility changed */
            if (h && find_overlay(h)) {
                char cls[32] = "";
                GetClassNameA(h, cls, sizeof cls);
                if (is_owner_draw_button(h, cls)) render_label(h);
            }
            break;
        }
        }
    }
    return CallNextHookEx(g_ret, code, wp, lp);
}

BOOL WINAPI fix_SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    Map m;
    if (g_in_swp) return real_SetWindowPos(hwnd, after, x, y, cx, cy, flags);
    if (g_enabled && hwnd && (GetWindowLongA(hwnd, GWL_STYLE) & WS_CHILD)) {
        HWND parent = GetParent(hwnd);
        if (parent && get_child_map(parent, &m)) {
            int *px = (flags & SWP_NOMOVE) ? NULL : &x, *py = (flags & SWP_NOMOVE) ? NULL : &y;
            int *pw = (flags & SWP_NOSIZE) ? NULL : &cx, *ph = (flags & SWP_NOSIZE) ? NULL : &cy;
            if (g_verbose) shim_log("SetWindowPos child %p (%d,%d %dx%d) flags=%#x [scaled]", (void *)hwnd, x, y, cx, cy, flags);
            map_rect(&m, px, py, pw, ph);
        }
    }
    g_in_swp = 1;
    BOOL r = g_next_SetWindowPos(hwnd, after, x, y, cx, cy, flags);
    g_in_swp = 0;
    return r;
}

/* The game believes its controls still have their design size: report design-space client sizes. */
BOOL WINAPI fix_GetClientRect(HWND hwnd, LPRECT r) {
    if (g_in_gcr) return real_GetClientRect(hwnd, r);
    g_in_gcr = 1;
    BOOL ok = g_next_GetClientRect(hwnd, r);
    g_in_gcr = 0;
    Map m;
    if (ok && g_enabled && hwnd && r && (GetWindowLongA(hwnd, GWL_STYLE) & WS_CHILD)) {
        HWND parent = GetParent(hwnd);
        if (parent && get_child_map(parent, &m)) {
            r->right = (int)(r->right / m.sx + 0.5);
            r->bottom = (int)(r->bottom / m.sy + 0.5);
        }
    }
    return ok;
}

/* ... and design-space window rectangles (screen-relative, as the real API would report in exclusive fullscreen). */
static void to_design_rect(HWND hwnd, HWND parent, const Map *m, LPRECT r) {
    WINDOWINFO wi = {sizeof wi};
    GetWindowInfo(GetAncestor(parent, GA_ROOT), &wi);
    int Ox = wi.rcClient.left, Oy = wi.rcClient.top;
    if (GetAncestor(parent, GA_ROOT) != parent) {   /* nested control: relative to its own (scaled) dialog */
        RECT pr;
        real_GetWindowRect(parent, &pr);
        Ox = pr.left; Oy = pr.top;
    }
    int l = (int)((r->left - Ox - m->ox) / m->sx + 0.5) + Ox, t = (int)((r->top - Oy - m->oy) / m->sy + 0.5) + Oy;
    int w = (int)((r->right - r->left) / m->sx + 0.5), h = (int)((r->bottom - r->top) / m->sy + 0.5);
    SetRect(r, l, t, l + w, t + h);
    (void)hwnd;
}

BOOL WINAPI fix_GetWindowRect(HWND hwnd, LPRECT r) {
    if (g_in_gwr) return real_GetWindowRect(hwnd, r);
    g_in_gwr = 1;
    BOOL ok = g_next_GetWindowRect(hwnd, r);
    g_in_gwr = 0;
    Map m;
    if (ok && g_enabled && hwnd && r && (GetWindowLongA(hwnd, GWL_STYLE) & WS_CHILD)) {
        HWND parent = GetParent(hwnd);
        if (parent && get_child_map(parent, &m)) to_design_rect(hwnd, parent, &m, r);
    }
    return ok;
}

/* Call-site hook (patches.c) for the owner-draw painter, which feeds GetWindowRect straight into DirectDraw blits. */
BOOL WINAPI fix_GetWindowRect_game_space(HWND hwnd, LPRECT r) {
    BOOL ok = real_GetWindowRect(hwnd, r);
    Map m;
    HWND parent = hwnd ? GetParent(hwnd) : NULL;
    if (ok && g_enabled && parent && (GetWindowLongA(hwnd, GWL_STYLE) & WS_CHILD) && get_child_map(parent, &m))
        to_design_rect(hwnd, parent, &m, r);
    return ok;
}

void window_fix_install(void) {
    HMODULE u = GetModuleHandleA("user32.dll");
    real_SetWindowPos = (void *)GetProcAddress(u, "SetWindowPos");
    real_GetClientRect = (void *)GetProcAddress(u, "GetClientRect");
    real_GetWindowRect = (void *)GetProcAddress(u, "GetWindowRect");
    DWORD tid = GetCurrentThreadId();
    g_cbt = SetWindowsHookExA(WH_CBT, cbt_proc, NULL, tid);
    g_ret = SetWindowsHookExA(WH_CALLWNDPROCRET, ret_proc, NULL, tid);
    if (g_render_labels) SetTimer(NULL, 0, 50, repaint_tick);   /* runs on this (the game's) thread */
    shim_log("window fix: hooks CBT=%p RET=%p (thread %lu)", (void *)g_cbt, (void *)g_ret, (unsigned long)tid);
}


