/*
 * Per-user registry redirection.
 *
 * Gangsters keeps its settings under HKLM\SOFTWARE\Hothouse\Gangsters and writes them back, which fails for a
 * standard user (GOG works around it by forcing "run as administrator"). We redirect the game's HKLM\SOFTWARE\Hothouse
 * keys to HKCU\Software\Hothouse, seeding the per-user copy from the machine-wide one on first use.
 */
#include <windows.h>
#include <string.h>
#include "mci_music.h"

LONG (WINAPI *g_next_RegOpenKeyExA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
LONG (WINAPI *g_next_RegCreateKeyExA)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);

static int g_reg_verbose;
void registry_redirect_configure(int verbose) { g_reg_verbose = verbose; }

/* If (hkey, sub) names a Hothouse key under HKLM, writes the HKCU sub-path to out and returns 1. */
static int redirect(HKEY hkey, LPCSTR sub, char *out, size_t n, LPCSTR *orig_sub) {
    static const char prefix[] = "SOFTWARE\\Hothouse";
    if (hkey != HKEY_LOCAL_MACHINE || !sub || _strnicmp(sub, prefix, sizeof prefix - 1)) return 0;
    wsprintfA(out, "Software\\%s", sub + 9);   /* skip "SOFTWARE\" */
    *orig_sub = sub;
    (void)n;
    return 1;
}

static void seed(LPCSTR hklm_sub, LPCSTR hkcu_sub) {
    HKEY test;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, hkcu_sub, 0, KEY_READ, &test) == ERROR_SUCCESS) { RegCloseKey(test); return; }
    HKEY src;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, hklm_sub, 0, KEY_READ, &src) != ERROR_SUCCESS) return;
    HKEY dst;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, hkcu_sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &dst, NULL) == ERROR_SUCCESS) {
        /* copy values only: RegCopyTree would also copy the (read-only for users) security descriptor */
        int n = 0;
        for (DWORD i = 0;; i++) {
            char name[256];
            BYTE data[1024];
            DWORD nl = sizeof name, dl = sizeof data, type;
            if (RegEnumValueA(src, i, name, &nl, NULL, &type, data, &dl) != ERROR_SUCCESS) break;
            if (RegSetValueExA(dst, name, 0, type, data, dl) == ERROR_SUCCESS) n++;
        }
        shim_log("registry: seeded HKCU\\%s from HKLM\\%s (%d values)", hkcu_sub, hklm_sub, n);
        RegCloseKey(dst);
    }
    RegCloseKey(src);
}

LONG WINAPI fix_RegOpenKeyExA(HKEY k, LPCSTR sub, DWORD opt, REGSAM sam, PHKEY out) {
    char path[512];
    LPCSTR orig;
    if (redirect(k, sub, path, sizeof path, &orig)) {
        seed(orig, path);
        LONG r = g_next_RegOpenKeyExA(HKEY_CURRENT_USER, path, opt, sam, out);
        if (g_reg_verbose) shim_log("RegOpenKeyEx HKLM\\%s -> HKCU\\%s = %ld", sub, path, r);
        return r;
    }
    return g_next_RegOpenKeyExA(k, sub, opt, sam, out);
}

LONG WINAPI fix_RegCreateKeyExA(HKEY k, LPCSTR sub, DWORD res, LPSTR cls, DWORD opt, REGSAM sam,
                                LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disp) {
    char path[512];
    LPCSTR orig;
    if (redirect(k, sub, path, sizeof path, &orig)) {
        seed(orig, path);
        LONG r = g_next_RegCreateKeyExA(HKEY_CURRENT_USER, path, res, cls, opt, sam, sa, out, disp);
        if (g_reg_verbose) shim_log("RegCreateKeyEx HKLM\\%s -> HKCU\\%s = %ld", sub, path, r);
        return r;
    }
    return g_next_RegCreateKeyExA(k, sub, res, cls, opt, sam, sa, out, disp);
}
