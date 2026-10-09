/* Diagnostic: trace winmm mixer API calls made by the game and GOG's PATCH.dll (CD volume control). */
#include <windows.h>
#include <mmsystem.h>
#include "mci_music.h"

static UINT (WINAPI *r_NumDevs)(void);
static MMRESULT (WINAPI *r_Open)(LPHMIXER, UINT, DWORD_PTR, DWORD_PTR, DWORD);
static MMRESULT (WINAPI *r_LineInfo)(HMIXEROBJ, LPMIXERLINEA, DWORD);
static MMRESULT (WINAPI *r_LineControls)(HMIXEROBJ, LPMIXERLINECONTROLSA, DWORD);
static MMRESULT (WINAPI *r_GetDetails)(HMIXEROBJ, LPMIXERCONTROLDETAILS, DWORD);
static MMRESULT (WINAPI *r_SetDetails)(HMIXEROBJ, LPMIXERCONTROLDETAILS, DWORD);

static int g_single_mixer;

UINT WINAPI t_mixerGetNumDevs(void) {
    UINT n = r_NumDevs();
    shim_log("mixerGetNumDevs -> %u%s", n, g_single_mixer && n > 1 ? " (reporting 1)" : "");
    return g_single_mixer && n > 1 ? 1 : n;
}

void mixer_set_single(int on) { g_single_mixer = on; }
MMRESULT WINAPI t_mixerOpen(LPHMIXER h, UINT id, DWORD_PTR cb, DWORD_PTR inst, DWORD flags) {
    MMRESULT r = r_Open(h, id, cb, inst, flags);
    shim_log("mixerOpen(id=%u flags=%#lx) -> %u", id, flags, r);
    return r;
}
MMRESULT WINAPI t_mixerGetLineInfoA(HMIXEROBJ h, LPMIXERLINEA l, DWORD flags) {
    DWORD type = l->dwComponentType, dest = l->dwDestination;
    MMRESULT r = r_LineInfo(h, l, flags);
    shim_log("mixerGetLineInfo(flags=%#lx reqComponentType=%#lx dest=%lu) -> %u line='%s' id=%lu controls=%lu connections=%lu", flags,
             type, dest, r, r ? "" : l->szName, r ? 0 : l->dwLineID, r ? 0 : l->cControls, r ? 0 : l->cConnections);
    return r;
}
MMRESULT WINAPI t_mixerGetLineControlsA(HMIXEROBJ h, LPMIXERLINECONTROLSA c, DWORD flags) {
    MMRESULT r = r_LineControls(h, c, flags);
    shim_log("mixerGetLineControls(flags=%#lx line=%lu) -> %u", flags, c->dwLineID, r);
    return r;
}
MMRESULT WINAPI t_mixerGetControlDetailsA(HMIXEROBJ h, LPMIXERCONTROLDETAILS d, DWORD flags) {
    MMRESULT r = r_GetDetails(h, d, flags);
    shim_log("mixerGetControlDetails(control=%lu) -> %u", d->dwControlID, r);
    return r;
}
MMRESULT WINAPI t_mixerSetControlDetails(HMIXEROBJ h, LPMIXERCONTROLDETAILS d, DWORD flags) {
    MMRESULT r = r_SetDetails(h, d, flags);
    shim_log("mixerSetControlDetails(control=%lu) -> %u", d->dwControlID, r);
    return r;
}

void mixer_trace_install(HMODULE mod) {
    HMODULE w = GetModuleHandleA("winmm.dll");
    r_NumDevs = (void *)GetProcAddress(w, "mixerGetNumDevs");
    r_Open = (void *)GetProcAddress(w, "mixerOpen");
    r_LineInfo = (void *)GetProcAddress(w, "mixerGetLineInfoA");
    r_LineControls = (void *)GetProcAddress(w, "mixerGetLineControlsA");
    r_GetDetails = (void *)GetProcAddress(w, "mixerGetControlDetailsA");
    r_SetDetails = (void *)GetProcAddress(w, "mixerSetControlDetails");
    void *ig = NULL;
    int n = 0;
    n += hook_iat(mod, "winmm.dll", "mixerGetNumDevs", (void *)t_mixerGetNumDevs, &ig) == 1;
    n += hook_iat(mod, "winmm.dll", "mixerOpen", (void *)t_mixerOpen, &ig) == 1;
    n += hook_iat(mod, "winmm.dll", "mixerGetLineInfoA", (void *)t_mixerGetLineInfoA, &ig) == 1;
    n += hook_iat(mod, "winmm.dll", "mixerGetLineControlsA", (void *)t_mixerGetLineControlsA, &ig) == 1;
    n += hook_iat(mod, "winmm.dll", "mixerGetControlDetailsA", (void *)t_mixerGetControlDetailsA, &ig) == 1;
    n += hook_iat(mod, "winmm.dll", "mixerSetControlDetails", (void *)t_mixerSetControlDetails, &ig) == 1;
    shim_log("mixer trace: %d hooks installed in module %p", n, (void *)mod);
}
