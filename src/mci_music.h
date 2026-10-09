#pragma once
#include <windows.h>
#include <mmsystem.h>

void shim_log(const char *fmt, ...);
int hook_iat(HMODULE mod, const char *dll, const char *func, void *replacement, void **original);
void mci_music_init(const char *game_dir);
void mci_set_autoplay(int track);
void mci_set_trace(int on);
void apply_exe_patches(void);
void patches_debug_cd_dump(int on);

BOOL WINAPI fix_SetWindowPos(HWND, HWND, int, int, int, int, UINT);
extern BOOL (WINAPI *g_next_SetWindowPos)(HWND, HWND, int, int, int, int, UINT);
BOOL WINAPI fix_GetWindowRect(HWND, LPRECT);
extern BOOL (WINAPI *g_next_GetWindowRect)(HWND, LPRECT);
BOOL WINAPI fix_GetClientRect(HWND, LPRECT);
extern BOOL (WINAPI *g_next_GetClientRect)(HWND, LPRECT);
void mixer_trace_install(HMODULE mod);
void mixer_set_single(int on);
LONG WINAPI fix_RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
LONG WINAPI fix_RegCreateKeyExA(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
extern LONG (WINAPI *g_next_RegOpenKeyExA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
extern LONG (WINAPI *g_next_RegCreateKeyExA)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
void registry_redirect_configure(int verbose);
void window_fix_configure(int enabled, int maintain_ar, int verbose);
void window_fix_install(void);
void window_fix_set_render_labels(int on);

MCIERROR WINAPI mci_send_command(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR param);
BOOL WINAPI mci_get_error_string(MCIERROR err, LPSTR buf, UINT len);

HANDLE WINAPI trace_CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL WINAPI trace_ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
extern HANDLE (WINAPI *g_next_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
extern BOOL (WINAPI *g_next_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

MCIERROR WINAPI mci_exe_logger(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR param);
extern MCIERROR (WINAPI *g_exe_next_mciSendCommandA)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);

extern MCIERROR (WINAPI *g_real_mciSendCommandA)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
extern BOOL (WINAPI *g_real_mciGetErrorStringA)(MCIERROR, LPSTR, UINT);



