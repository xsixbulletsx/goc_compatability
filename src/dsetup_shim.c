/*
 * DSETUP.dll shim for Gangsters: Organized Crime.
 *
 * gangsters.exe imports the (obsolete) DirectX setup DLL from its own folder, which makes it a reliable
 * injection point: we ship this DLL as DSETUP.dll, forward all of its exports to the original (renamed
 * DSETUP_orig.dll), and use DllMain to hook the game's MCI music calls.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "mci_music.h"

/* Lets the installer recognise this DLL (as opposed to the original DSETUP.dll). Keep the text stable. */
__declspec(dllexport) const char goc_patch_marker[] = "GOC_PATCH_SHIM_V1";

static FILE *g_log;
static char g_ini_path[MAX_PATH];   /* gangsters_patch.ini next to the exe */

void shim_log(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    fprintf(g_log, "[%lu] ", GetCurrentThreadId());
    vfprintf(g_log, fmt, ap); fputc('\n', g_log); fflush(g_log);
    va_end(ap);
}

/* Replace one import (by name) of `dll` in module `mod`'s IAT. Returns 1 if patched. */
int hook_iat(HMODULE mod, const char *dll, const char *func, void *replacement, void **original) {
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (lstrcmpiA((const char *)(base + d->Name), dll)) continue;
        IMAGE_THUNK_DATA *oft = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *ft = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(base + oft->u1.AddressOfData);
            if (strcmp((const char *)n->Name, func)) continue;
            DWORD old;
            if ((void *)ft->u1.Function == replacement) return 2;   /* already ours */
            VirtualProtect(&ft->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            if (original) *original = (void *)ft->u1.Function;      /* whoever is there now (system or another hook) */
            ft->u1.Function = (DWORD_PTR)replacement;
            VirtualProtect(&ft->u1.Function, sizeof(void *), old, &old);
            return 1;
        }
    }
    return 0;
}

/* Other components (GOG's PATCH.dll, cnc-ddraw) patch the exe's mciSendCommandA import again after startup. Keep our
 * game-facing hook in front, chaining to whatever is currently installed behind it. */
static VOID CALLBACK reassert_exe_logger(HWND w, UINT m, UINT_PTR id, DWORD now) {
    (void)w; (void)m; (void)id; (void)now;
    void *next = NULL;
    int r = hook_iat(GetModuleHandleA(NULL), "winmm.dll", "mciSendCommandA", (void *)mci_exe_logger, &next);
    if (r == 1 && next) {
        g_exe_next_mciSendCommandA = (void *)next;
        shim_log("game MCI hook re-asserted (the import had been replaced); chaining to %p", next);
    }
}

static void install_hooks(void) {
    /* GOG's PATCH.dll maps the game's CD-audio commands to Music\N.mp3 MCI calls; hook its imports so that
     * mapping stays intact. Without PATCH.dll (non-GOG copy) hook the exe directly. Never hook both: PATCH.dll
     * may wrap the exe's import at runtime, which would recurse. */
    HMODULE m = GetModuleHandleA("PATCH.dll");
    const char *which = "PATCH.dll";
    if (!m) { m = GetModuleHandleA(NULL); which = "exe"; }
    void *ignore = NULL;
    int a = hook_iat(m, "winmm.dll", "mciSendCommandA", (void *)mci_send_command, &ignore);
    int b = hook_iat(m, "winmm.dll", "mciGetErrorStringA", (void *)mci_get_error_string, &ignore);
    shim_log("hooked %s: mciSendCommandA=%d mciGetErrorStringA=%d", which, a, b);

    if (GetPrivateProfileIntA("compat", "registry_redirect", 1, g_ini_path)) {
        HMODULE exe = GetModuleHandleA(NULL);
        registry_redirect_configure(GetPrivateProfileIntA("debug", "trace_registry", 0, g_ini_path));
        int r1 = hook_iat(exe, "ADVAPI32.dll", "RegOpenKeyExA", (void *)fix_RegOpenKeyExA, (void **)&g_next_RegOpenKeyExA);
        int r2 = hook_iat(exe, "ADVAPI32.dll", "RegCreateKeyExA", (void *)fix_RegCreateKeyExA, (void **)&g_next_RegCreateKeyExA);
        shim_log("registry redirect hooks: open=%d create=%d", r1, r2);
    }

    if (GetPrivateProfileIntA("display", "scale_child_windows", 1, g_ini_path)) {
        HMODULE exe = GetModuleHandleA(NULL);
        char ddini[MAX_PATH];
        lstrcpyA(ddini, g_ini_path);
        char *sl = strrchr(ddini, '\\');
        if (sl) lstrcpyA(sl + 1, "ddraw.ini");
        char v[16];
        GetPrivateProfileStringA("ddraw", "maintas", "false", v, sizeof v, ddini);
        window_fix_configure(1, !lstrcmpiA(v, "true") || !lstrcmpA(v, "1"),
                             GetPrivateProfileIntA("debug", "trace_windows", 0, g_ini_path));
        int c2 = hook_iat(exe, "USER32.dll", "SetWindowPos", (void *)fix_SetWindowPos, (void **)&g_next_SetWindowPos);
        int c3 = hook_iat(exe, "USER32.dll", "GetClientRect", (void *)fix_GetClientRect, (void **)&g_next_GetClientRect);
        hook_iat(exe, "USER32.dll", "GetWindowRect", (void *)fix_GetWindowRect, (void **)&g_next_GetWindowRect);
        window_fix_set_render_labels(GetPrivateProfileIntA("display", "render_button_labels", 1, g_ini_path));
        window_fix_install();
        shim_log("child window scaling: SetWindowPos hook=%d GetClientRect hook=%d", c2, c3);
    }

    if (GetPrivateProfileIntA("debug", "trace_mixer", 0, g_ini_path) || GetPrivateProfileIntA("compat", "single_mixer", 0, g_ini_path)) {
        mixer_set_single(GetPrivateProfileIntA("compat", "single_mixer", 0, g_ini_path));
        mixer_trace_install(GetModuleHandleA(NULL));
        mixer_trace_install(GetModuleHandleA("PATCH.dll"));
    }


    if (GetPrivateProfileIntA("debug", "trace_files", 0, g_ini_path)) {
        HMODULE exe = GetModuleHandleA(NULL);
        int c1 = hook_iat(exe, "KERNEL32.dll", "CreateFileA", (void *)trace_CreateFileA, (void **)&g_next_CreateFileA);
        int c2 = hook_iat(exe, "KERNEL32.dll", "ReadFile", (void *)trace_ReadFile, (void **)&g_next_ReadFile);
        shim_log("file trace hooks: CreateFileA=%d ReadFile=%d", c1, c2);
    }

    /* The game's own MCI calls: route them straight to PATCH.dll's CD emulation (see mci_music.c). */
    if (m != GetModuleHandleA(NULL)) {
        mci_set_trace(GetPrivateProfileIntA("debug", "trace_mci", 0, g_ini_path));
        int c = hook_iat(GetModuleHandleA(NULL), "winmm.dll", "mciSendCommandA", (void *)mci_exe_logger, (void **)&g_exe_next_mciSendCommandA);
        shim_log("game MCI hook=%d next=%p", c, (void *)g_exe_next_mciSendCommandA);
        SetTimer(NULL, 0, 1500, reassert_exe_logger);
    }
}

/* Describe an address as module+offset for crash reports. */
static void describe_addr(DWORD_PTR a, char *out, size_t n) {
    HMODULE m = NULL;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m)) {
        GetModuleFileNameA(m, name, MAX_PATH);
        const char *s = strrchr(name, '\\');
        snprintf(out, n, "%s+0x%lx", s ? s + 1 : name, (unsigned long)(a - (DWORD_PTR)m));
    } else {
        snprintf(out, n, "0x%lx", (unsigned long)a);
    }
}

static LONG CALLBACK crash_logger(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW && code != 0xC0000602)
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = ep->ContextRecord;
    char where[300];
    describe_addr((DWORD_PTR)ep->ExceptionRecord->ExceptionAddress, where, sizeof where);
    shim_log("*** EXCEPTION %#lx at %s", (unsigned long)code, where);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        shim_log("    %s address %#lx", ep->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read of",
                 (unsigned long)ep->ExceptionRecord->ExceptionInformation[1]);
    shim_log("    eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
             c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    if (!IsBadReadPtr((void *)(c->Ebp - 0x50), 0x80)) {
        DWORD *a = (DWORD *)(c->Ebp + 4);
        shim_log("    ret=%08lx args: %08lx %08lx %08lx %08lx %08lx %08lx", a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        shim_log("    [ebp-0x28]=%08lx (sprite frame count) [ebp-8]=%08lx", *(DWORD *)(c->Ebp - 0x28), *(DWORD *)(c->Ebp - 8));
        shim_log("    hdr words: w=%d h=%d x=%d y=%d  %d %d  off=%08lx", *(short *)(c->Ebp - 0x40), *(short *)(c->Ebp - 0x3e),
                 *(short *)(c->Ebp - 0x3c), *(short *)(c->Ebp - 0x3a), *(short *)(c->Ebp - 0x38), *(short *)(c->Ebp - 0x36),
                 *(DWORD *)(c->Ebp - 0x30));
        DWORD obj = *(DWORD *)(c->Ebp - 4), idx = *(DWORD *)(c->Ebp - 0xc);
        shim_log("    this=%08lx fileidx=%lu", obj, idx);
    }
    DWORD *sp = (DWORD *)c->Esp;
    for (int i = 0; i < 96 && !IsBadReadPtr(sp + i, 4); i++) {
        DWORD v = sp[i];
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void *)v, &mbi, sizeof mbi) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) && mbi.Type == MEM_IMAGE) {
            describe_addr(v, where, sizeof where);
            shim_log("    stack[%02d] %s", i, where);
        }
    }
    if (g_log) fflush(g_log);
    return EXCEPTION_CONTINUE_SEARCH;
}

static char g_game_dir[MAX_PATH];

/* Everything that patches the game: runs on the game's main thread once all DLLs are initialised. */
static void install_all(void) {
    mci_music_init(g_game_dir);
    install_hooks();
    mci_set_autoplay(GetPrivateProfileIntA("debug", "autoplay_track", 0, g_ini_path));
    apply_exe_patches();
    patches_debug_cd_dump(GetPrivateProfileIntA("debug", "dump_cd_object", 0, g_ini_path));
}

#ifdef GOC_AS_DDRAW
static void (WINAPI *g_next_GetStartupInfoA)(LPSTARTUPINFOA);
static void WINAPI late_get_startup_info(LPSTARTUPINFOA si) {
    static int done;
    if (!done) {
        done = 1;
        /* put the original import back first so nothing else sees our stub */
        void *self = NULL;
        hook_iat(GetModuleHandleA(NULL), "KERNEL32.dll", "GetStartupInfoA", (void *)g_next_GetStartupInfoA, &self);
        shim_log("late install on thread %lu", (unsigned long)GetCurrentThreadId());
        install_all();
    }
    g_next_GetStartupInfoA(si);
}
#endif

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        char p[MAX_PATH];
        DisableThreadLibraryCalls(h);
        GetModuleFileNameA(h, p, MAX_PATH);
        char *s = strrchr(p, '\\');
        if (s) *s = 0;
        wsprintfA(g_ini_path, "%s\\gangsters_patch.ini", p);
        char logpath[MAX_PATH];
        wsprintfA(logpath, "%s\\gangsters_patch.log", p);
        g_log = fopen(logpath, "w");
#ifdef GOC_AS_DDRAW
        /* Loaded as ddraw.dll (a static dependency of GOG's PATCH.dll, so it runs before the game starts); all
         * DirectDraw exports are forwarded to cnc-ddraw.dll by the loader. Nothing of the game is replaced. */
        shim_log("shim attached as ddraw.dll (forwarding to cnc-ddraw.dll)");
#else
        char orig[MAX_PATH];
        wsprintfA(orig, "%s\\DSETUP_orig.dll", p);
        HMODULE o = LoadLibraryA(orig);   /* resolves our forwarded exports */
        shim_log("shim attached as DSETUP.dll; DSETUP_orig=%p", (void *)o);
#endif
        AddVectoredExceptionHandler(1, crash_logger);
        lstrcpyA(g_game_dir, p);
#ifdef GOC_AS_DDRAW
        /* Our DllMain runs before PATCH.dll's and cnc-ddraw's, and both re-patch the very imports we hook. So wait
         * until all DLL initialisation is over: the game's CRT startup calls GetStartupInfoA on the main thread
         * right after, which is where we install everything. */
        hook_iat(GetModuleHandleA(NULL), "KERNEL32.dll", "GetStartupInfoA", (void *)late_get_startup_info, (void **)&g_next_GetStartupInfoA);
        shim_log("installation deferred to game startup (GetStartupInfoA hook %p)", (void *)g_next_GetStartupInfoA);
#else
        install_all();
#endif
    }
    return TRUE;
}



