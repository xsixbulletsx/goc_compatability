/*
 * MCI music replacement. GOG's PATCH.dll turns the game's CD-audio commands into mciSendCommandA calls on
 * Music\N.mp3. Windows 11 routes those through DirectShow, whose AC3 decoder fail-fasts and kills the process.
 * We intercept mp3 devices and play them with miniaudio instead; everything else is forwarded to winmm.
 */
#include "mci_music.h"
#include "miniaudio.h"
#include <stdio.h>
#include <string.h>

MCIERROR (WINAPI *g_real_mciSendCommandA)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
BOOL (WINAPI *g_real_mciGetErrorStringA)(MCIERROR, LPSTR, UINT);

#define MAX_DEV 8
#define DEV_BASE 0x7A00

typedef struct {
    int used;
    ma_sound snd;
    int snd_ok;
    HWND notify_wnd;        /* pending MCI_NOTIFY target for the current play */
    volatile LONG playing;
    volatile LONG finished; /* set by the audio thread at end of stream */
    int paused;
    DWORD stop_ms;          /* 0 = play to end */
} Dev;

static Dev g_dev[MAX_DEV];
static ma_engine g_engine;
static int g_engine_ok, g_engine_tried;
static char g_game_dir[MAX_PATH];
static CRITICAL_SECTION g_cs;
static int g_cs_init;

/* Debug: [debug] autoplay_track=N starts Music\N.mp3 a few seconds after launch, to test the audio chain. */
static int g_autoplay_track;
static VOID CALLBACK autoplay_tick(HWND w, UINT m, UINT_PTR id, DWORD now) {
    (void)w; (void)m; (void)now;
    KillTimer(NULL, id);
    char name[MAX_PATH];
    wsprintfA(name, "%s\\Music\\%d.mp3", g_game_dir, g_autoplay_track);
    MCI_OPEN_PARMSA op = {0};
    op.lpstrElementName = name;
    MCIERROR r = mci_send_command(0, MCI_OPEN, MCI_OPEN_ELEMENT, (DWORD_PTR)&op);
    shim_log("autoplay: open %s -> %#lx dev=%u", name, (unsigned long)r, op.wDeviceID);
    if (!r) {
        MCI_PLAY_PARMS pp = {0};
        r = mci_send_command(op.wDeviceID, MCI_PLAY, 0, (DWORD_PTR)&pp);
        shim_log("autoplay: play -> %#lx", (unsigned long)r);
    }
}
void mci_set_autoplay(int track) {
    g_autoplay_track = track;
    if (track > 0) SetTimer(NULL, 0, 6000, autoplay_tick);
}

void mci_music_init(const char *game_dir) {
    lstrcpynA(g_game_dir, game_dir, MAX_PATH);
    InitializeCriticalSection(&g_cs);
    g_cs_init = 1;
    HMODULE w = GetModuleHandleA("winmm.dll");
    if (!w) w = LoadLibraryA("winmm.dll");
    g_real_mciSendCommandA = (void *)GetProcAddress(w, "mciSendCommandA");
    g_real_mciGetErrorStringA = (void *)GetProcAddress(w, "mciGetErrorStringA");
}

static int ensure_engine(void) {
    if (!g_engine_tried) {
        g_engine_tried = 1;
        ma_engine_config cfg = ma_engine_config_init();
        g_engine_ok = ma_engine_init(&cfg, &g_engine) == MA_SUCCESS;
        shim_log("miniaudio engine init: %s", g_engine_ok ? "ok" : "FAILED");
    }
    return g_engine_ok;
}

static int is_mp3_name(const char *n) {
    size_t l = n ? strlen(n) : 0;
    return l > 4 && !lstrcmpiA(n + l - 4, ".mp3");
}

static Dev *dev_from_id(MCIDEVICEID id) {
    if (id < DEV_BASE || id >= DEV_BASE + MAX_DEV) return NULL;
    Dev *d = &g_dev[id - DEV_BASE];
    return d->used ? d : NULL;
}

static void notify(Dev *d, MCIDEVICEID id, WPARAM how) {
    HWND w = d->notify_wnd;
    d->notify_wnd = NULL;
    if (w) PostMessageA(w, MM_MCINOTIFY, how, (LPARAM)id);
}

static void on_end(void *user, ma_sound *s) {
    Dev *d = (Dev *)user;
    (void)s;
    InterlockedExchange(&d->finished, 1);
    /* notification is sent from the game thread's next MCI call or poll; post directly, it is thread safe */
    notify(d, (MCIDEVICEID)(DEV_BASE + (d - g_dev)), MCI_NOTIFY_SUCCESSFUL);
    InterlockedExchange(&d->playing, 0);
}

static void dev_close(Dev *d, MCIDEVICEID id, WPARAM how) {
    if (d->snd_ok) {
        ma_sound_stop(&d->snd);
        ma_sound_uninit(&d->snd);
    }
    notify(d, id, how);
    memset(d, 0, sizeof *d);
}

static MCIERROR do_open(MCI_OPEN_PARMSA *p, DWORD_PTR flags) {
    char path[MAX_PATH];
    const char *name = p->lpstrElementName;
    if (!ensure_engine()) return MCIERR_DEVICE_NOT_READY;
    if (GetFileAttributesA(name) == INVALID_FILE_ATTRIBUTES && name[0] && name[1] != ':') {
        wsprintfA(path, "%s\\%s", g_game_dir, name);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) name = path;
    }
    int slot = -1;
    for (int i = 0; i < MAX_DEV; i++) if (!g_dev[i].used) { slot = i; break; }
    if (slot < 0) return MCIERR_OUT_OF_MEMORY;
    Dev *d = &g_dev[slot];
    memset(d, 0, sizeof *d);

    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, name, -1, wpath, MAX_PATH);
    if (ma_sound_init_from_file_w(&g_engine, wpath, MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, NULL, &d->snd) != MA_SUCCESS) {
        shim_log("  miniaudio could not open %s", name);
        return MCIERR_FILE_NOT_FOUND;
    }
    d->snd_ok = 1;
    d->used = 1;
    ma_sound_set_end_callback(&d->snd, on_end, d);
    p->wDeviceID = (MCIDEVICEID)(DEV_BASE + slot);
    return 0;
}

static MCIERROR do_play(Dev *d, MCIDEVICEID id, DWORD_PTR flags, MCI_PLAY_PARMS *p) {
    if (d->notify_wnd) notify(d, id, MCI_NOTIFY_SUPERSEDED);
    if (p && (flags & MCI_FROM)) ma_sound_seek_to_second(&d->snd, p->dwFrom / 1000.0f);
    else if (!d->paused && ma_sound_at_end(&d->snd)) ma_sound_seek_to_pcm_frame(&d->snd, 0);
    d->stop_ms = (p && (flags & MCI_TO)) ? p->dwTo : 0;
    if (flags & MCI_NOTIFY) d->notify_wnd = (HWND)(p ? p->dwCallback : 0);
    d->paused = 0;
    InterlockedExchange(&d->finished, 0);
    InterlockedExchange(&d->playing, 1);
    ma_sound_start(&d->snd);
    if (flags & MCI_WAIT) {
        while (d->playing && !d->finished) Sleep(20);
    }
    return 0;
}

static DWORD pos_ms(Dev *d) {
    float c = 0;
    ma_sound_get_cursor_in_seconds(&d->snd, &c);
    return (DWORD)(c * 1000.0f);
}

static DWORD len_ms(Dev *d) {
    float l = 0;
    ma_sound_get_length_in_seconds(&d->snd, &l);
    return (DWORD)(l * 1000.0f);
}

MCIERROR WINAPI mci_send_command(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR param) {
    if (!g_cs_init) return g_real_mciSendCommandA(id, msg, flags, param);

    /* new device: only intercept mp3 elements */
    if (msg == MCI_OPEN && param) {
        MCI_OPEN_PARMSA *p = (MCI_OPEN_PARMSA *)param;
        if ((flags & MCI_OPEN_ELEMENT) && is_mp3_name(p->lpstrElementName)) {
            EnterCriticalSection(&g_cs);
            MCIERROR r = do_open(p, flags);
            LeaveCriticalSection(&g_cs);
            shim_log("OPEN mp3 '%s' -> %#lx id=%u", p->lpstrElementName, (unsigned long)r, p->wDeviceID);
            return r;
        }
        MCIERROR r = g_real_mciSendCommandA(id, msg, flags, param);
        shim_log("OPEN passthrough flags=%#lx type=%s elem=%s -> %#lx", (unsigned long)flags,
                 (flags & MCI_OPEN_TYPE) && (ULONG_PTR)p->lpstrDeviceType >= 0x10000 ? p->lpstrDeviceType : "-",
                 (flags & MCI_OPEN_ELEMENT) ? p->lpstrElementName : "-", (unsigned long)r);
        return r;
    }

    EnterCriticalSection(&g_cs);
    Dev *d = dev_from_id(id);
    if (!d) {
        LeaveCriticalSection(&g_cs);
        if (id == MCI_ALL_DEVICE_ID) {   /* e.g. close/stop everything */
            EnterCriticalSection(&g_cs);
            for (int i = 0; i < MAX_DEV; i++)
                if (g_dev[i].used && (msg == MCI_CLOSE || msg == MCI_STOP))
                    { if (msg == MCI_CLOSE) dev_close(&g_dev[i], DEV_BASE + i, MCI_NOTIFY_ABORTED); else ma_sound_stop(&g_dev[i].snd); }
            LeaveCriticalSection(&g_cs);
        }
        MCIERROR r = g_real_mciSendCommandA(id, msg, flags, param);
        shim_log("passthrough id=%u msg=%#x flags=%#lx -> %#lx", id, msg, (unsigned long)flags, (unsigned long)r);
        return r;
    }

    MCIERROR r = 0;
    switch (msg) {
    case MCI_PLAY:
        r = do_play(d, id, flags, (MCI_PLAY_PARMS *)param);
        shim_log("PLAY id=%u flags=%#lx from=%lu to=%lu", id, (unsigned long)flags,
                 (flags & MCI_FROM) ? ((MCI_PLAY_PARMS *)param)->dwFrom : 0, (flags & MCI_TO) ? ((MCI_PLAY_PARMS *)param)->dwTo : 0);
        break;
    case MCI_STOP:
        ma_sound_stop(&d->snd);
        InterlockedExchange(&d->playing, 0);
        d->paused = 0;
        if (d->notify_wnd) notify(d, id, MCI_NOTIFY_ABORTED);
        ma_sound_seek_to_pcm_frame(&d->snd, 0);
        shim_log("STOP id=%u", id);
        break;
    case MCI_PAUSE:
        ma_sound_stop(&d->snd);
        d->paused = 1;
        shim_log("PAUSE id=%u", id);
        break;
    case MCI_RESUME:
        ma_sound_start(&d->snd);
        d->paused = 0;
        shim_log("RESUME id=%u", id);
        break;
    case MCI_SEEK: {
        MCI_SEEK_PARMS *p = (MCI_SEEK_PARMS *)param;
        if (flags & MCI_SEEK_TO_START) ma_sound_seek_to_pcm_frame(&d->snd, 0);
        else if ((flags & MCI_TO) && p) ma_sound_seek_to_second(&d->snd, p->dwTo / 1000.0f);
        shim_log("SEEK id=%u flags=%#lx", id, (unsigned long)flags);
        break;
    }
    case MCI_CLOSE:
        dev_close(d, id, MCI_NOTIFY_ABORTED);
        shim_log("CLOSE id=%u", id);
        break;
    case MCI_SET:
        shim_log("SET id=%u flags=%#lx (ignored)", id, (unsigned long)flags);
        break;
    case MCI_STATUS: {
        MCI_STATUS_PARMS *p = (MCI_STATUS_PARMS *)param;
        if (!(flags & MCI_STATUS_ITEM) || !p) break;
        switch (p->dwItem) {
        case MCI_STATUS_MODE:
            p->dwReturn = d->paused ? MCI_MODE_PAUSE : (d->playing && !d->finished ? MCI_MODE_PLAY : MCI_MODE_STOP);
            break;
        case MCI_STATUS_POSITION: p->dwReturn = pos_ms(d); break;
        case MCI_STATUS_LENGTH: p->dwReturn = len_ms(d); break;
        case MCI_STATUS_READY: p->dwReturn = TRUE; break;
        case MCI_STATUS_NUMBER_OF_TRACKS: p->dwReturn = 1; break;
        case MCI_STATUS_CURRENT_TRACK: p->dwReturn = 1; break;
        case MCI_STATUS_TIME_FORMAT: p->dwReturn = MCI_FORMAT_MILLISECONDS; break;
        case MCI_STATUS_MEDIA_PRESENT: p->dwReturn = TRUE; break;
        default:
            p->dwReturn = 0;
            shim_log("STATUS id=%u unknown item %#lx", id, (unsigned long)p->dwItem);
        }
        break;
    }
    default:
        shim_log("UNHANDLED msg=%#x id=%u flags=%#lx (returning success)", msg, id, (unsigned long)flags);
    }
    LeaveCriticalSection(&g_cs);
    return r;
}

typedef MCIERROR (WINAPI *mci_fn)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);

/* PATCH.dll's MCI dispatcher: push ebp; mov ebp,esp; mov eax,[ebp+0xc]; add eax,0xfffff7fd (msg - 0x803). Only trust
 * it if those bytes are exactly there, so a different build of the DLL is left alone. */
static mci_fn find_patch_dispatch(void) {
    static int checked;
    static mci_fn fn;
    if (!checked) {
        static const BYTE sig[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x05, 0xfd, 0xf7, 0xff, 0xff};
        HMODULE pm = GetModuleHandleA("PATCH.dll");
        if (pm) {
            BYTE *p = (BYTE *)pm + 0x2c70;
            if (!IsBadReadPtr(p, sizeof sig) && !memcmp(p, sig, sizeof sig)) fn = (mci_fn)p;
            checked = 1;
            shim_log("PATCH.dll MCI dispatcher %s", fn ? "located (direct routing enabled)" : "NOT recognised; using the normal import chain");
        }
    }
    return fn;
}

/*
 * Game-facing MCI entry point (installed on the exe's mciSendCommandA import).
 *
 * GOG's PATCH.dll emulates the CD drive (fake device id, tracks -> Music\N.mp3) by detouring winmm. On current
 * Windows that detour does not reliably see the game's calls, and cnc-ddraw (which hooks mciSendCommandA for MCI
 * video fixes) replaces PATCH.dll's own import afterwards, diverting its mp3 playback to the real MCI where it fails
 * silently. The visible result is a game that never plays music. So:
 *   1. call PATCH.dll's dispatcher directly (located by signature, so another build is left alone), and
 *   2. put our mp3 player back in front of PATCH.dll's import before each command.
 */
MCIERROR (WINAPI *g_exe_next_mciSendCommandA)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
static int g_trace_mci;
void mci_set_trace(int on) { g_trace_mci = on; }

#define PATCH_FAKE_CD_ID 7777461   /* device id PATCH.dll hands out for the emulated CD */

MCIERROR WINAPI mci_exe_logger(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR param) {
    if (g_trace_mci) {
        if (msg == MCI_OPEN && param) {
            MCI_OPEN_PARMSA *p = (MCI_OPEN_PARMSA *)param;
            shim_log("GAME OPEN flags=%#lx type=%s elem=%s", (unsigned long)flags,
                     (flags & MCI_OPEN_TYPE) && (ULONG_PTR)p->lpstrDeviceType >= 0x10000 ? p->lpstrDeviceType : "-",
                     (flags & MCI_OPEN_ELEMENT) ? p->lpstrElementName : "-");
        } else if (msg == MCI_PLAY && param) {
            MCI_PLAY_PARMS *p = (MCI_PLAY_PARMS *)param;
            shim_log("GAME PLAY id=%u flags=%#lx from=%#lx to=%#lx", id, (unsigned long)flags, (unsigned long)p->dwFrom, (unsigned long)p->dwTo);
        } else if (msg == MCI_STATUS && param) {
            MCI_STATUS_PARMS *p = (MCI_STATUS_PARMS *)param;
            shim_log("GAME STATUS id=%u item=%#lx track=%lu", id, (unsigned long)p->dwItem, (unsigned long)p->dwTrack);
        } else {
            shim_log("GAME CMD id=%u msg=%#x flags=%#lx", id, msg, (unsigned long)flags);
        }
    }

    MCIERROR r;
    mci_fn patch_dispatch = find_patch_dispatch();
    if (patch_dispatch) {
        void *prev = NULL;
        if (hook_iat(GetModuleHandleA("PATCH.dll"), "winmm.dll", "mciSendCommandA", (void *)mci_send_command, &prev) == 1)
            shim_log("PATCH.dll's mciSendCommandA import had been replaced (%p); mp3 player re-installed in front", prev);
        r = patch_dispatch(id, msg, flags, param);
        if (r == MCIERR_UNSUPPORTED_FUNCTION && id != PATCH_FAKE_CD_ID && g_exe_next_mciSendCommandA)
            r = g_exe_next_mciSendCommandA(id, msg, flags, param);   /* not a CD command: let the system handle it */
    } else {
        r = g_exe_next_mciSendCommandA(id, msg, flags, param);
    }

    if (g_trace_mci) {
        if (msg == MCI_STATUS && param) shim_log("GAME   -> %#lx return=%lu", (unsigned long)r, (unsigned long)((MCI_STATUS_PARMS *)param)->dwReturn);
        else shim_log("GAME   -> %#lx", (unsigned long)r);
    }
    return r;
}
BOOL WINAPI mci_get_error_string(MCIERROR e, LPSTR buf, UINT n) {
    return g_real_mciGetErrorStringA(e, buf, n);
}

