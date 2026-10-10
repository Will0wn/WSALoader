// WSASmartTV: mod for WSALoader, runs inside WsaClient (no extra process).
//  - Density: answers WsaClient with the matching scale, so Android starts with that density
//  - Fullscreen: sends F11 once to every new Android app window
//  - Remote: web page on the local network (remote.html) that sends keys to the Android app
// Configuration: config.ini next to this DLL. Log: %TEMP%\WSASmartTV.log
// Crafted by Will0wn - https://github.com/Will0wn
#include <winsock2.h>
#include <windows.h>
#include <xinput.h>
#include <stdio.h>

static int g_dpi;          // 0 = leave the scale alone
static int g_fullscreen;
static wchar_t g_log[MAX_PATH];
static wchar_t g_dir[MAX_PATH];     // folder of this DLL
static int g_port;                  // 0 = remote off
static char g_pin[40];              // empty = no PIN
static wchar_t g_home[128];         // package opened by the Home key

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

typedef UINT (WINAPI *GetDpiForWindow_t)(HWND);
typedef HRESULT (WINAPI *GetDpiForMonitor_t)(HMONITOR, int, UINT *, UINT *);
typedef BOOL (WINAPI *ShowWindow_t)(HWND, int);
static GetDpiForWindow_t real_GetDpiForWindow;
static GetDpiForMonitor_t real_GetDpiForMonitor;
static ShowWindow_t real_ShowWindow;

static UINT WINAPI my_GetDpiForWindow(HWND h)
{
    return g_dpi ? (UINT)g_dpi : real_GetDpiForWindow(h);
}

static HRESULT WINAPI my_GetDpiForMonitor(HMONITOR m, int type, UINT *x, UINT *y)
{
    HRESULT r = real_GetDpiForMonitor(m, type, x, y);
    if (g_dpi && SUCCEEDED(r)) { *x = g_dpi; *y = g_dpi; }
    return r;
}

// Windows already sent to fullscreen
static HWND g_done[256];
static int g_ndone;

static DWORD WINAPI send_f11(LPVOID p)
{
    HWND h = (HWND)p;
    Sleep(800);
    if (!IsWindow(h)) return 0;
    PostMessageW(h, WM_KEYDOWN, VK_F11, 0x00570001);
    PostMessageW(h, WM_KEYUP, VK_F11, 0xC0570001);
    logf("F11 sent to %p", (void *)h);
    return 0;
}

static BOOL WINAPI my_ShowWindow(HWND h, int cmd)
{
    BOOL r = real_ShowWindow(h, cmd);
    if (g_fullscreen && cmd != SW_HIDE && cmd != SW_MINIMIZE && !GetParent(h)) {
        wchar_t cls[256];
        GetClassNameW(h, cls, 256);
        // the class of app windows is the Android package name; the splash screen
        // ("<package>(splash)") is skipped: in fullscreen it got stuck when the app closed
        if (wcschr(cls, L'.') && !wcsstr(cls, L"(splash)")) {
            for (int i = 0; i < g_ndone; i++) if (g_done[i] == h) return r;
            if (g_ndone < 256) g_done[g_ndone++] = h;
            logf("new window %ls (%p)", cls, (void *)h);
            CloseHandle(CreateThread(NULL, 0, send_f11, h, 0, NULL));
        }
    }
    return r;
}

// Replaces a function in WsaClient's delay-load import table, by name (fn) or by ordinal (fn = NULL)
static void *patch_delay(HMODULE mod, const char *dll, const char *fn, WORD ord, void *repl)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!dd.VirtualAddress) return NULL;
    for (IMAGE_DELAYLOAD_DESCRIPTOR *d = (void *)(base + dd.VirtualAddress); d->DllNameRVA; d++) {
        if (lstrcmpiA((char *)(base + d->DllNameRVA), dll)) continue;
        IMAGE_THUNK_DATA *names = (void *)(base + d->ImportNameTableRVA);
        IMAGE_THUNK_DATA *iat = (void *)(base + d->ImportAddressTableRVA);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                if (fn || IMAGE_ORDINAL(names->u1.Ordinal) != ord) continue;
            } else {
                IMAGE_IMPORT_BY_NAME *ibn = (void *)(base + names->u1.AddressOfData);
                if (!fn || lstrcmpA((char *)ibn->Name, fn)) continue;
            }
            DWORD old;
            VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            iat->u1.Function = (ULONG_PTR)repl;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            return repl;
        }
    }
    return NULL;
}

// ---- Virtual gamepad ----
// Android TV players confirm a seek with DPAD_CENTER, not ENTER. Android maps the gamepad button A
// to DPAD_CENTER (fallback in Generic.kcm), and WsaClient reads Xbox controllers through XInput:
// the remote OK key presses A on a virtual controller in slot PAD_SLOT, "connected" on first use.

#define PAD_SLOT 3

typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *XInputGetCapabilities_t)(DWORD, DWORD, XINPUT_CAPABILITIES *);
static XInputGetState_t real_XInputGetState;
static XInputGetCapabilities_t real_XInputGetCapabilities;
static volatile LONG g_pad_connected;
static volatile WORD g_pad_buttons;
static volatile DWORD g_pad_packet;
static volatile ULONGLONG g_pad_polled;     // last time WsaClient asked for PAD_SLOT
static BOOL g_pad_hooked;                   // both XInput hooks installed

static DWORD WINAPI my_XInputGetState(DWORD slot, XINPUT_STATE *s)
{
    if (slot != PAD_SLOT) return real_XInputGetState(slot, s);
    if (!g_pad_polled) logf("gamepad: WsaClient polls XInput");
    g_pad_polled = GetTickCount64();
    if (!g_pad_connected) return real_XInputGetState(slot, s);
    ZeroMemory(s, sizeof *s);
    s->dwPacketNumber = g_pad_packet;
    s->Gamepad.wButtons = g_pad_buttons;
    return ERROR_SUCCESS;
}

static DWORD WINAPI my_XInputGetCapabilities(DWORD slot, DWORD flags, XINPUT_CAPABILITIES *c)
{
    if (slot != PAD_SLOT || !g_pad_connected) return real_XInputGetCapabilities(slot, flags, c);
    ZeroMemory(c, sizeof *c);
    c->Type = XINPUT_DEVTYPE_GAMEPAD;
    c->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    c->Gamepad.wButtons = 0xF3FF;
    return ERROR_SUCCESS;
}

// TRUE if WsaClient is polling XInput (otherwise the pad would never be seen)
static BOOL pad_available(void)
{
    return g_pad_hooked && g_pad_polled && GetTickCount64() - g_pad_polled < 5000;
}

static void pad_press(WORD button)
{
    if (!g_pad_connected) {
        InterlockedExchange(&g_pad_connected, 1);
        logf("gamepad: virtual controller connected in slot %d", PAD_SLOT);
        Sleep(1500);   // time for WsaClient to see it and announce it to Android
    }
    g_pad_buttons = button;
    g_pad_packet++;
    Sleep(120);
    g_pad_buttons = 0;
    g_pad_packet++;
}

// ---- Remote ----

// Window of an Android app in this process (the class is the package name), splash excluded
static BOOL is_app_window(HWND h)
{
    DWORD pid;
    wchar_t cls[256];
    if (!IsWindowVisible(h) || GetParent(h)) return FALSE;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return FALSE;
    GetClassNameW(h, cls, 256);
    return wcschr(cls, L'.') && !wcsstr(cls, L"(splash)");
}

static BOOL CALLBACK find_app(HWND h, LPARAM out)
{
    if (!is_app_window(h)) return TRUE;
    *(HWND *)out = h;
    return FALSE;
}

// The foreground app if it is ours, otherwise the topmost open app
static HWND target_window(void)
{
    HWND h = GetForegroundWindow();
    if (h && is_app_window(h)) return h;
    h = NULL;
    EnumWindows(find_app, (LPARAM)&h);
    return h;
}

static void post_key(HWND h, UINT vk, UINT sc, BOOL ext)
{
    LPARAM l = 1 | (sc << 16) | (ext ? 1 << 24 : 0);
    PostMessageW(h, WM_KEYDOWN, vk, l);
    PostMessageW(h, WM_KEYUP, vk, l | 0xC0000000);
}

// System key (volume): goes to the Windows mixer, not to the window
static void system_key(WORD vk)
{
    INPUT in[2] = {0};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// Home: opens the launcher like the Start menu shortcuts (WsaClient.exe /launch wsa://<package>)
static void launch_home(void)
{
    typedef LONG (WINAPI *family_t)(UINT32 *, PWSTR);
    family_t family = (family_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFamilyName");
    wchar_t exe[MAX_PATH], fam[128], cmd[MAX_PATH + 160];
    UINT32 n = 128;
    // the WindowsApps alias starts WsaClient with the package identity
    if (family && family(&n, fam) == 0 && GetEnvironmentVariableW(L"LOCALAPPDATA", exe, MAX_PATH))
        _snwprintf(exe + lstrlenW(exe), MAX_PATH - lstrlenW(exe), L"\\Microsoft\\WindowsApps\\%ls\\WsaClient.exe", fam);
    else
        GetModuleFileNameW(NULL, exe, MAX_PATH);
    _snwprintf(cmd, MAX_PATH + 160, L"\"%ls\" /launch wsa://%ls", exe, g_home);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        logf("home: launching %ls failed (%lu)", g_home, GetLastError());
    }
}

// Runs a key; returns the text for the page, NULL if the key does not exist
static const char *do_key(const char *c)
{
    static const struct { const char *name; UINT vk, sc; BOOL ext; } keys[] = {
        { "up", VK_UP, 0x48, TRUE },       { "down", VK_DOWN, 0x50, TRUE },
        { "left", VK_LEFT, 0x4B, TRUE },   { "right", VK_RIGHT, 0x4D, TRUE },
        { "ok", VK_RETURN, 0x1C, FALSE },  { "back", VK_ESCAPE, 0x01, FALSE },
        { "playpause", VK_MEDIA_PLAY_PAUSE, 0x22, TRUE },
    };
    if (!strcmp(c, "ping")) return "ok";
    if (!strcmp(c, "ok") && pad_available()) { pad_press(XINPUT_GAMEPAD_A); return "ok"; }
    if (!strcmp(c, "home")) { launch_home(); return "ok"; }
    if (!strcmp(c, "volup")) { system_key(VK_VOLUME_UP); return "ok"; }
    if (!strcmp(c, "voldown")) { system_key(VK_VOLUME_DOWN); return "ok"; }
    if (!strcmp(c, "mute")) { system_key(VK_VOLUME_MUTE); return "ok"; }
    for (int i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
        if (strcmp(c, keys[i].name)) continue;
        HWND h = target_window();
        if (!h) return "no app open";
        post_key(h, keys[i].vk, keys[i].sc, keys[i].ext);
        return "ok";
    }
    return NULL;
}

static void reply(SOCKET s, const char *status, const char *type, const char *body, int len)
{
    char head[256];
    int n = snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, type, len);
    send(s, head, n, 0);
    send(s, body, len, 0);
}

static void reply_text(SOCKET s, const char *status, const char *text)
{
    reply(s, status, "text/plain; charset=utf-8", text, (int)strlen(text));
}

// Value of parameter <key> in the query "a=1&b=2"
static void query_param(const char *q, const char *key, char *out, int cap)
{
    size_t kl = strlen(key);
    out[0] = 0;
    for (const char *p = q; p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, kl) || p[kl] != '=') continue;
        int i = 0;
        for (p += kl + 1; *p && *p != '&' && i < cap - 1; p++) out[i++] = *p;
        out[i] = 0;
        return;
    }
}

// The page is remote.html next to the DLL: it can be edited without recompiling
static void serve_page(SOCKET s)
{
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\remote.html", g_dir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size = f == INVALID_HANDLE_VALUE ? 0 : GetFileSize(f, NULL), got = 0;
    char *buf = size && size < (1 << 20) ? HeapAlloc(GetProcessHeap(), 0, size) : NULL;
    if (buf && ReadFile(f, buf, size, &got, NULL) && got == size)
        reply(s, "200 OK", "text/html; charset=utf-8", buf, (int)size);
    else
        reply_text(s, "500 Internal Server Error", "remote.html missing");
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
}

static void handle(SOCKET s, const char *path)
{
    if (!strcmp(path, "/")) { serve_page(s); return; }
    if (strncmp(path, "/k?", 3)) { reply_text(s, "404 Not Found", "?"); return; }
    char c[32], pin[sizeof g_pin];
    query_param(path + 3, "c", c, sizeof c);
    query_param(path + 3, "p", pin, sizeof pin);
    if (g_pin[0] && strcmp(pin, g_pin)) {
        Sleep(1000);   // slows down PIN guessing
        reply_text(s, "403 Forbidden", "Wrong PIN");
        return;
    }
    const char *r = do_key(c);
    if (r) reply_text(s, "200 OK", r);
    else reply_text(s, "400 Bad Request", "unknown key");
}

static DWORD WINAPI remote_server(LPVOID unused)
{
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)g_port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 8)) {
        logf("remote: port %d not available (error %d)", g_port, WSAGetLastError());
        closesocket(ls);
        return 0;
    }
    logf("remote listening on port %d%s", g_port, g_pin[0] ? " with PIN" : " without PIN");
    for (;;) {
        SOCKET s = accept(ls, NULL, NULL);
        if (s == INVALID_SOCKET) { Sleep(100); continue; }
        DWORD timeout = 3000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
        char buf[2048];
        int n = recv(s, buf, sizeof buf - 1, 0);
        if (n > 4 && !strncmp(buf, "GET ", 4)) {
            buf[n] = 0;
            char *path = buf + 4, *end = strchr(path, ' ');
            if (end) *end = 0;
            handle(s, path);
        }
        closesocket(s);
    }
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);

    GetTempPathW(MAX_PATH, g_log);
    lstrcatW(g_log, L"WSASmartTV.log");

    wchar_t ini[MAX_PATH];
    GetModuleFileNameW(self, g_dir, MAX_PATH);
    *wcsrchr(g_dir, L'\\') = 0;
    _snwprintf(ini, MAX_PATH, L"%ls\\config.ini", g_dir);
    int density = GetPrivateProfileIntW(L"WSASmartTV", L"Density", 0, ini);
    g_fullscreen = GetPrivateProfileIntW(L"WSASmartTV", L"Fullscreen", 1, ini);
    g_dpi = density ? MulDiv(density, 96, 160) : 0;   // Android density = Windows dpi / 96 * 160

    wchar_t pin[sizeof g_pin];
    g_port = GetPrivateProfileIntW(L"WSASmartTV", L"RemotePort", 0, ini);
    GetPrivateProfileStringW(L"WSASmartTV", L"RemotePin", L"", pin, sizeof g_pin, ini);
    WideCharToMultiByte(CP_UTF8, 0, pin, -1, g_pin, sizeof g_pin, NULL, NULL);
    GetPrivateProfileStringW(L"WSASmartTV", L"RemoteHome", L"com.spocky.projengmenu", g_home, 128, ini);
    if (g_port) CloseHandle(CreateThread(NULL, 0, remote_server, NULL, 0, NULL));

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    real_GetDpiForWindow = (GetDpiForWindow_t)GetProcAddress(user32, "GetDpiForWindow");
    real_ShowWindow = (ShowWindow_t)GetProcAddress(user32, "ShowWindow");
    real_GetDpiForMonitor = (GetDpiForMonitor_t)GetProcAddress(shcore, "GetDpiForMonitor");

    HMODULE exe = GetModuleHandleW(NULL);
    void *a = patch_delay(exe, "USER32.dll", "GetDpiForWindow", 0, my_GetDpiForWindow);
    void *b = patch_delay(exe, "api-ms-win-shcore-scaling-l1-1-1.dll", "GetDpiForMonitor", 0, my_GetDpiForMonitor);
    void *c = patch_delay(exe, "USER32.dll", "ShowWindow", 0, my_ShowWindow);

    // virtual gamepad for the remote OK key: XInputGetState = ordinal 2, XInputGetCapabilities = 4
    if (g_port) {
        HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
        real_XInputGetState = (XInputGetState_t)GetProcAddress(xinput, (LPCSTR)2);
        real_XInputGetCapabilities = (XInputGetCapabilities_t)GetProcAddress(xinput, (LPCSTR)4);
        // both hooks or none: the pad is used only if WsaClient sees both functions replaced
        if (real_XInputGetState && real_XInputGetCapabilities) {
            void *s = patch_delay(exe, "XINPUT1_4.dll", NULL, 2, my_XInputGetState);
            void *k = patch_delay(exe, "XINPUT1_4.dll", NULL, 4, my_XInputGetCapabilities);
            g_pad_hooked = s && k;
        }
        logf("gamepad hook: %d", g_pad_hooked);
    }
    logf("loaded: density=%d dpi=%d fullscreen=%d hook dpiWin=%d dpiMon=%d show=%d",
         density, g_dpi, g_fullscreen, a != NULL, b != NULL, c != NULL);
    return TRUE;
}
