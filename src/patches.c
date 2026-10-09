/*
 * In-memory binary fixes for gangsters.exe (v1.0.0.1, timestamp 0x3726cffd, image base 0x400000).
 * Every patch verifies the original bytes first, so it silently does nothing on a different build.
 */
#include <windows.h>
#include <string.h>
#include "mci_music.h"

#define EXE_BASE 0x400000

/*
 * Fix 1: sprite frame blit (0x53fb?? "draw frame N of a .spr file").
 * The routine reads frame N's header (w,h,x,y,...) and only validates w*h <= 0x1900. If N is out of range
 * (the character-appearance code can request frame == frame count) the header is pixel data; w == 0 makes the
 * copy loop `dec ecx; jne` run 2^32 times and read off the end of the stack. Reject non-positive w or h as
 * well, which takes the routine's normal "return 0" failure path.
 *   0x53fca9: cmp ecx,0x1900 ; jle 0x53fcbc      (8 bytes)   -> jmp stub ; nop nop nop
 *   0x53fcb1: xor eax,eax ... ret 0x18           (failure return)
 *   0x53fcbc: normal path
 */
__asm__(
    ".globl _sprite_check_stub\n"
    "_sprite_check_stub:\n"
    "  cmpw $0, -0x40(%ebp)\n"      /* w */
    "  jle 1f\n"
    "  cmpw $0, -0x3e(%ebp)\n"      /* h */
    "  jle 1f\n"
    "  cmpl $0x1900, %ecx\n"        /* ecx = w*h */
    "  jg 1f\n"
    "  pushl $0x53fcbc\n"
    "  ret\n"
    "1:\n"
    "  pushl $0x53fcb1\n"
    "  ret\n");
extern char sprite_check_stub[];

static int patch(DWORD va, const BYTE *expect, const BYTE *repl, size_t n, const char *what) {
    BYTE *p = (BYTE *)va;
    if (memcmp(p, expect, n)) {
        shim_log("patch '%s' skipped: bytes at %#lx do not match (different exe build?)", what, (unsigned long)va);
        return 0;
    }
    DWORD old;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(p, repl, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    shim_log("patch '%s' applied at %#lx", what, (unsigned long)va);
    return 1;
}

/*
 * Fix 2: owner-drawn button painter (0x642e40). It calls GetWindowRect(button) and uses the screen left/top as
 * DirectDraw surface coordinates for the button art, which is only right when the control sits at its design
 * position. We move/scale controls to match the upscaled picture, so route this one call through
 * fix_GetWindowRect_game_space(), which reports design-space coordinates.
 *   0x642ecd: call dword ptr [0x905e58]   (FF 15 58 5E 90 00)
 */
extern BOOL WINAPI fix_GetWindowRect_game_space(HWND, LPRECT);
static BOOL (WINAPI *g_gwr_game_space_ptr)(HWND, LPRECT) = fix_GetWindowRect_game_space;

/*
 * Fix 3: the same painter ends with a single TextOutA that draws the button label at screen resolution straight onto
 * whatever DC it was given. We render the labels ourselves (overlay popups), and that duplicate flashes onto the
 * screen whenever the game triggers the painter. Turn that one call into a no-op (the DrawTextA calls before it
 * only measure text).
 *   0x64386e: call dword ptr [0x905ac0]   (FF 15 C0 5A 90 00)   ; TextOutA
 */
static BOOL WINAPI noop_text_out(HDC dc, int x, int y, LPCSTR s, int n) { (void)dc; (void)x; (void)y; (void)s; (void)n; return TRUE; }
static BOOL (WINAPI *g_noop_text_out_ptr)(HDC, int, int, LPCSTR, int) = noop_text_out;

/* Debug: periodically dump the game's CD-player object (global pointer at 0x7c001c) to the log. */
static VOID CALLBACK cd_dump_tick(HWND w, UINT m, UINT_PTR id, DWORD now) {
    (void)w; (void)m; (void)id; (void)now;
    DWORD *pp = (DWORD *)0x7c001c;
    if (IsBadReadPtr(pp, 4) || !*pp || IsBadReadPtr((void *)*pp, 0x80)) { shim_log("cd object: g_cd=%08lx (null/unreadable)", IsBadReadPtr(pp, 4) ? 0 : *pp); return; }
    DWORD *o = (DWORD *)*pp;
    shim_log("cd object %p: curTrack(+8)=%lu tracks(+28)=%lu dev(+2c)=%lu opened(+34)=%lu playing(+38)=%lu paused(+3c)=%lu enabled(+40)=%lu req(+48)=%lu",
             (void *)o, o[2], o[0x28 / 4], o[0x2c / 4], o[0x34 / 4], o[0x38 / 4], o[0x3c / 4], o[0x40 / 4], o[0x48 / 4]);
}
void patches_debug_cd_dump(int on) { if (on) SetTimer(NULL, 0, 4000, cd_dump_tick); }

void apply_exe_patches(void) {
    if ((DWORD_PTR)GetModuleHandleA(NULL) != EXE_BASE) {
        shim_log("exe not at expected base, skipping binary patches");
        return;
    }
    {
        static const BYTE expect[8] = {0x81, 0xf9, 0x00, 0x19, 0x00, 0x00, 0x7e, 0x0b};
        BYTE repl[8] = {0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
        DWORD rel = (DWORD)(DWORD_PTR)sprite_check_stub - (0x53fca9 + 5);
        memcpy(repl + 1, &rel, 4);
        patch(0x53fca9, expect, repl, 8, "sprite frame bounds check");
    }
    {
        static const BYTE expect[6] = {0xff, 0x15, 0x58, 0x5e, 0x90, 0x00};
        BYTE repl[6] = {0xff, 0x15, 0, 0, 0, 0};
        DWORD slot = (DWORD)(DWORD_PTR)&g_gwr_game_space_ptr;
        memcpy(repl + 2, &slot, 4);
        patch(0x642ecd, expect, repl, 6, "button painter GetWindowRect");
    }
    {
        static const BYTE expect[6] = {0xff, 0x15, 0xc0, 0x5a, 0x90, 0x00};
        BYTE repl[6] = {0xff, 0x15, 0, 0, 0, 0};
        DWORD slot = (DWORD)(DWORD_PTR)&g_noop_text_out_ptr;
        memcpy(repl + 2, &slot, 4);
        patch(0x64386e, expect, repl, 6, "button painter TextOutA (labels are drawn by the overlay)");
    }
}
