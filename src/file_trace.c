/* Diagnostic: trace CreateFileA and short/failed ReadFile calls made by the game executable. */
#include <windows.h>
#include <string.h>
#include "mci_music.h"

#define NH 256
static struct { HANDLE h; char name[96]; } g_h[NH];
static int g_nh;

HANDLE (WINAPI *g_next_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL (WINAPI *g_next_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

static const char *name_of(HANDLE h) {
    for (int i = 0; i < NH; i++) if (g_h[i].h == h) return g_h[i].name;
    return "?";
}

HANDLE WINAPI trace_CreateFileA(LPCSTR n, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
    HANDLE h = g_next_CreateFileA(n, a, s, sa, d, f, t);
    if (h != INVALID_HANDLE_VALUE) {
        int i = g_nh++ % NH;
        g_h[i].h = h;
        lstrcpynA(g_h[i].name, n ? n : "(null)", sizeof g_h[i].name);
    }
    shim_log("CreateFile '%s' access=%#lx disp=%lu -> %p (err %lu)", n ? n : "(null)", a, d, h,
             h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    return h;
}

BOOL WINAPI trace_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov) {
    DWORD tmp = 0;
    if (!got) got = &tmp;
    BOOL ok = g_next_ReadFile(h, buf, n, got, ov);
    if (!ok || *got != n)
        shim_log("ReadFile SHORT/FAIL h=%p '%s' want=%lu got=%lu ok=%d err=%lu", h, name_of(h), n, *got, ok, ok ? 0 : GetLastError());
    return ok;
}
