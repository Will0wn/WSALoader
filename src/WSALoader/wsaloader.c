// WSALoader: installed in place of <WSA>\gfxstream_backend.dll, makes WsaClient load mods.
// The 24 functions WsaClient imports are stubs that jump (jmp) to the real DLL, gfxstream_real.dll,
// loaded in DllMain by full path: it does not depend on the DLL search order.
// The other exports are forwarders in the .def, resolved once gfxstream_real.dll is already loaded.
// Mods: every folder <WSA>\Custom\<Name>\ that contains <Name>.dll (except WSALoader).
// Log: %TEMP%\WSALoader.log
// Crafted by Will0wn - https://github.com/Will0wn
#include <windows.h>
#include <stdio.h>

#define FNS(X) \
    X(stream_renderer_transfer_write_iov) \
    X(stream_renderer_get_cap_set) \
    X(stream_renderer_fill_caps) \
    X(gfxstream_set_latte_operations) \
    X(stream_renderer_resource_detach_iov) \
    X(stream_renderer_resource_map) \
    X(stream_renderer_create_blob) \
    X(stream_renderer_resource_create) \
    X(stream_renderer_resource_attach_iov) \
    X(stream_renderer_resource_unmap) \
    X(stream_renderer_ctx_detach_resource) \
    X(stream_renderer_ctx_attach_resource) \
    X(stream_renderer_context_destroy) \
    X(gfxstream_backend_create_channel) \
    X(stream_renderer_context_create) \
    X(stream_renderer_teardown) \
    X(stream_renderer_init) \
    X(gfxstream_set_d3d_device) \
    X(gfxstream_backend_channel_op) \
    X(stream_renderer_resource_unref) \
    X(stream_renderer_submit_cmd) \
    X(gfxstream_backend_close_channel) \
    X(stream_renderer_platform_resource_info) \
    X(stream_renderer_transfer_read_iov)

#define PTR(n) void *p_##n;
FNS(PTR)

#define STUB(n) __attribute__((naked)) __declspec(dllexport) void n(void) { __asm__ volatile("jmp *%0" :: "m"(p_##n)); }
FNS(STUB)

static wchar_t g_log[MAX_PATH];

static void logf(const char *fmt, ...)
{
    FILE *f = _wfopen(g_log, L"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f);
    fclose(f);
}

// Loads <custom>\<Name>\<Name>.dll for every folder, in alphabetical order
static void load_mods(const wchar_t *custom)
{
    wchar_t pattern[MAX_PATH], dll[MAX_PATH];
    WIN32_FIND_DATAW fd;
    _snwprintf(pattern, MAX_PATH, L"%ls\\*", custom);
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) { logf("mods folder not found: %ls", custom); return; }
    int n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' ||
            !lstrcmpiW(fd.cFileName, L"WSALoader")) continue;
        _snwprintf(dll, MAX_PATH, L"%ls\\%ls\\%ls.dll", custom, fd.cFileName, fd.cFileName);
        if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) continue;
        // the mod's dependencies are searched in its own folder first
        if (LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)) { logf("mod loaded: %ls", fd.cFileName); n++; }
        else logf("mod %ls NOT loaded (error %lu)", fd.cFileName, GetLastError());
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    logf("mods loaded: %d", n);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(self, p, MAX_PATH);
    *wcsrchr(p, L'\\') = 0;
    size_t dir = lstrlenW(p);

    // <WSA>\gfxstream_real.dll by full path; its dependencies (WSACodecs.dll) must be
    // searched in its folder, which is not in the process search path
    lstrcatW(p, L"\\gfxstream_real.dll");
    HMODULE real = LoadLibraryExW(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!real) return FALSE;
#define RES(n) if (!(p_##n = (void *)GetProcAddress(real, #n))) return FALSE;
    FNS(RES)

    // the installer test loads the loader in a test process: no mods there
    if (GetEnvironmentVariableW(L"WSALOADER_NOMODS", NULL, 0)) return TRUE;

    GetTempPathW(MAX_PATH, g_log);
    lstrcatW(g_log, L"WSALoader.log");
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    logf("WSALoader started in %ls - Crafted by Will0wn (github.com/Will0wn)", wcsrchr(exe, L'\\') + 1);

    // <WSA>\Custom
    p[dir] = 0;
    lstrcatW(p, L"\\Custom");
    load_mods(p);
    return TRUE;
}
