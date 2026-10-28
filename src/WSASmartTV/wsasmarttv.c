// WSASmartTV: mod for WSALoader, runs inside WsaClient (no extra process).
//  - Density: answers WsaClient with the matching scale, so Android starts with that density
//  - Fullscreen: sends F11 once to every new Android app window
//  - Remote: web page on the local network (remote.html) that sends keys to the Android app,
//    or (external mode) commands received from a server the mod connects to
// Configuration: config.ini next to this DLL. Log: %TEMP%\WSASmartTV.log
// Crafted by Will0wn - https://github.com/Will0wn
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <xinput.h>
#include <stdio.h>

enum { REMOTE_OFF, REMOTE_STANDALONE, REMOTE_EXTERNAL };

static int g_dpi;          // 0 = leave the scale alone
static int g_fullscreen;
static wchar_t g_log[MAX_PATH];
static wchar_t g_dir[MAX_PATH];     // folder of this DLL
static int g_mode;                  // REMOTE_*
static int g_port;                  // standalone: port of the page
static char g_pin[40];              // standalone: empty = no PIN
static wchar_t g_server[512];       // external: ws:// or wss:// URL of the server
static wchar_t g_token[128];        // external: sent as "Authorization: Bearer <token>"
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

// ---- Remote ----

// WSA's Back is Alt+Left. WsaClient reads the Alt state with GetKeyState: while the remote sends
// Back, the hook reports Alt as pressed, and the messages are sent synchronously.
typedef SHORT (WINAPI *GetKeyState_t)(int);
static GetKeyState_t real_GetKeyState;
static volatile LONG g_fake_alt;

static SHORT WINAPI my_GetKeyState(int vk)
{
    if (g_fake_alt && (vk == VK_MENU || vk == VK_LMENU)) return (SHORT)0x8000;
    return real_GetKeyState(vk);
}

static void send_alt_left(HWND h)
{
    const LPARAM alt = 1 | (0x38 << 16) | (1 << 29), left = 1 | (0x4B << 16) | (1 << 24) | (1 << 29);
    DWORD_PTR r;
    InterlockedExchange(&g_fake_alt, 1);
    SendMessageTimeoutW(h, WM_SYSKEYDOWN, VK_MENU, alt, SMTO_ABORTIFHUNG, 1000, &r);
    SendMessageTimeoutW(h, WM_SYSKEYDOWN, VK_LEFT, left, SMTO_ABORTIFHUNG, 1000, &r);
    SendMessageTimeoutW(h, WM_SYSKEYUP, VK_LEFT, left | 0xC0000000, SMTO_ABORTIFHUNG, 1000, &r);
    InterlockedExchange(&g_fake_alt, 0);
    SendMessageTimeoutW(h, WM_KEYUP, VK_MENU, (alt & ~(1 << 29)) | 0xC0000000, SMTO_ABORTIFHUNG, 1000, &r);
}

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

// Runs WsaClient.exe <args> through the WindowsApps alias, which starts it with the package identity
// (like the Start menu shortcuts; running WsaClient.exe directly fails with 0xc00004bc)
static BOOL wsa_command(const wchar_t *args)
{
    typedef LONG (WINAPI *family_t)(UINT32 *, PWSTR);
    family_t family = (family_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFamilyName");
    wchar_t exe[MAX_PATH], fam[128], cmd[MAX_PATH + 200];
    UINT32 n = 128;
    if (family && family(&n, fam) == 0 && GetEnvironmentVariableW(L"LOCALAPPDATA", exe, MAX_PATH))
        _snwprintf(exe + lstrlenW(exe), MAX_PATH - lstrlenW(exe), L"\\Microsoft\\WindowsApps\\%ls\\WsaClient.exe", fam);
    else
        GetModuleFileNameW(NULL, exe, MAX_PATH);
    _snwprintf(cmd, MAX_PATH + 200, L"\"%ls\" %ls", exe, args);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return FALSE;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

// Home: opens the launcher (WsaClient.exe /launch wsa://<package>)
static void launch_home(void)
{
    wchar_t args[160];
    _snwprintf(args, 160, L"/launch wsa://%ls", g_home);
    if (!wsa_command(args)) logf("home: launching %ls failed (%lu)", g_home, GetLastError());
}

// Power: turns WSA off a moment after the page got its answer (the remote stops with it)
static DWORD WINAPI power_off(LPVOID unused)
{
    Sleep(500);
    logf("remote: turning WSA off");
    if (!wsa_command(L"/shutdown")) logf("remote: shutdown failed (%lu)", GetLastError());
    return 0;
}
// ---- Virtual gamepad ----
// WSA forwards Xbox controllers (XInput) to Android's virtual_gamepad, where button A falls back to
// DPAD_CENTER (Generic.kcm): the confirm key of Android TV players. While the remote is on, slot
// PAD_SLOT reports a connected controller, and the OK key presses its A button. No network involved.

#define PAD_SLOT 3

typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *XInputGetCapabilities_t)(DWORD, DWORD, XINPUT_CAPABILITIES *);
static XInputGetState_t real_XInputGetState;
static XInputGetCapabilities_t real_XInputGetCapabilities;
static BOOL g_pad_on;
static volatile WORD g_pad_buttons;
static volatile DWORD g_pad_packet;
static volatile ULONGLONG g_pad_polled;    // last time WsaClient read PAD_SLOT
static volatile LONG g_seen_state, g_seen_caps;
static volatile LONG g_pad_reads;         // reads of PAD_SLOT, to know when a press has been seen

static DWORD WINAPI my_XInputGetState(DWORD slot, XINPUT_STATE *s)
{
    if (slot < 4 && !(InterlockedOr(&g_seen_state, 1 << slot) & 1 << slot)) logf("xinput: GetState slot %lu", slot);
    if (slot != PAD_SLOT || !g_pad_on) return real_XInputGetState(slot, s);
    static ULONGLONG first; static int polls;   // measures how often WsaClient reads the pad
    if (!first) first = GetTickCount64();
    if (++polls == 200) logf("xinput: slot %d read every %llu ms", PAD_SLOT, (GetTickCount64() - first) / 199);
    g_pad_polled = GetTickCount64();
    ZeroMemory(s, sizeof *s);
    s->dwPacketNumber = g_pad_packet;
    s->Gamepad.wButtons = g_pad_buttons;
    InterlockedIncrement(&g_pad_reads);
    return ERROR_SUCCESS;
}

static DWORD WINAPI my_XInputGetCapabilities(DWORD slot, DWORD flags, XINPUT_CAPABILITIES *c)
{
    if (slot < 4 && !(InterlockedOr(&g_seen_caps, 1 << slot) & 1 << slot)) logf("xinput: GetCapabilities slot %lu", slot);
    if (slot != PAD_SLOT || !g_pad_on) return real_XInputGetCapabilities(slot, flags, c);
    ZeroMemory(c, sizeof *c);
    c->Type = XINPUT_DEVTYPE_GAMEPAD;
    c->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    c->Gamepad.wButtons = 0xF3FF;
    return ERROR_SUCCESS;
}

// Waits until WsaClient has read the pad at least twice (so one read saw the current state)
static BOOL pad_wait_reads(void)
{
    LONG start = g_pad_reads;
    ULONGLONG t0 = GetTickCount64();
    while (g_pad_reads - start < 2) {
        if (GetTickCount64() - t0 > 500) return FALSE;
        Sleep(1);
    }
    return TRUE;
}

// Presses a gamepad button: held until WsaClient has read it, then released the same way
static BOOL pad_press(WORD button)
{
    if (!g_pad_on) return FALSE;
    ULONGLONG t0 = GetTickCount64();
    g_pad_buttons = button;
    g_pad_packet++;
    BOOL down = pad_wait_reads();
    Sleep(20);
    g_pad_buttons = 0;
    g_pad_packet++;
    BOOL up = pad_wait_reads();
    logf("ok: A %s, release %s (%llu ms)", down ? "read" : "NOT read", up ? "read" : "NOT read", GetTickCount64() - t0);
    return TRUE;
}
// Runs a key; returns the text for the page, NULL if the key does not exist
static const char *do_key(const char *c)
{
    static const struct { const char *name; UINT vk, sc; BOOL ext; } keys[] = {
        { "up", VK_UP, 0x48, TRUE },       { "down", VK_DOWN, 0x50, TRUE },
        { "left", VK_LEFT, 0x4B, TRUE },   { "right", VK_RIGHT, 0x4D, TRUE },
        { "ok", VK_RETURN, 0x1C, FALSE },  { "enter", VK_RETURN, 0x1C, FALSE },
        { "backspace", VK_BACK, 0x0E, FALSE },
        { "playpause", VK_MEDIA_PLAY_PAUSE, 0x22, TRUE },
    };
    if (!strcmp(c, "ping")) return "ok";
    if (!strcmp(c, "ok") && pad_press(XINPUT_GAMEPAD_A)) return "ok";   // otherwise Enter, below
    if (!strcmp(c, "home")) { launch_home(); return "ok"; }
    if (!strcmp(c, "close")) {   // like the window X button: WSA ends the app, it restarts from scratch
        HWND h = target_window();
        if (!h) return "no app open";
        PostMessageW(h, WM_CLOSE, 0, 0);   // in fullscreen the window has no system menu: SC_CLOSE may be ignored
        Sleep(1500);
        logf("close: window %p %s", (void *)h, IsWindow(h) ? "STILL OPEN" : "closed");
        return "ok";
    }
    if (!strcmp(c, "power")) { CloseHandle(CreateThread(NULL, 0, power_off, NULL, 0, NULL)); return "ok"; }
    if (!strcmp(c, "back")) { HWND h = target_window(); if (!h) return "no app open"; send_alt_left(h); return "ok"; }
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

// Where an answer goes: the HTTP connection (standalone, buf = NULL) or the message for the server
// (external: "<id>\n<status>\n<content type>\n<body>", the id is already in buf)
typedef struct { SOCKET s; char *buf; int len, cap; } out_t;

static void reply(out_t *o, const char *status, const char *type, const char *body, int len)
{
    if (o->buf) {
        int n = snprintf(o->buf + o->len, o->cap - o->len, "%s\n%s\n", status, type);
        if (n < 0 || o->len + n + len > o->cap) {
            o->len += snprintf(o->buf + o->len, o->cap - o->len, "500 Internal Server Error\ntext/plain\ntoo big");
            return;
        }
        o->len += n;
        memcpy(o->buf + o->len, body, len);
        o->len += len;
        return;
    }
    char head[256];
    int n = snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, type, len);
    send(o->s, head, n, 0);
    send(o->s, body, len, 0);
}

static void reply_text(out_t *o, const char *status, const char *text)
{
    reply(o, status, "text/plain; charset=utf-8", text, (int)strlen(text));
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

static BOOL serve_file(out_t *o, const wchar_t *path, const char *type)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size = f == INVALID_HANDLE_VALUE ? 0 : GetFileSize(f, NULL), got = 0;
    char *buf = size && size < (1 << 20) ? HeapAlloc(GetProcessHeap(), 0, size) : NULL;
    BOOL ok = buf && ReadFile(f, buf, size, &got, NULL) && got == size;
    if (ok) reply(o, "200 OK", type, buf, (int)size);
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    return ok;
}

// The page is remote.html next to the DLL: it can be edited without recompiling
static void serve_page(out_t *o)
{
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\remote.html", g_dir);
    if (!serve_file(o, path, "text/html; charset=utf-8")) reply_text(o, "500 Internal Server Error", "remote.html missing");
}

// ---- Installed apps ----
// WSA keeps an icon per installed app in its LocalState folder (<package>.png): the remote shows
// them in a row and opens the chosen one like the Start menu shortcuts (/launch wsa://<package>).

// %LOCALAPPDATA%\Packages\<package family of WSA>\LocalState
static BOOL app_folder(wchar_t *dir)
{
    typedef LONG (WINAPI *family_t)(UINT32 *, PWSTR);
    family_t family = (family_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFamilyName");
    wchar_t fam[128];
    UINT32 n = 128;
    if (!family || family(&n, fam) || !GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) return FALSE;
    _snwprintf(dir + lstrlenW(dir), MAX_PATH - lstrlenW(dir), L"\\Packages\\%ls\\LocalState", fam);
    return TRUE;
}

// Android package names only: letters, digits, dots, underscores (nothing else reaches a command line)
static BOOL valid_package(const char *p)
{
    int n = 0;
    for (; p[n]; n++)
        if (!(p[n] >= 'a' && p[n] <= 'z') && !(p[n] >= 'A' && p[n] <= 'Z') && !(p[n] >= '0' && p[n] <= '9') &&
            p[n] != '.' && p[n] != '_') return FALSE;
    return n > 0 && n < 120;
}

// Icon of an installed package; FALSE if the name is invalid or the app is not installed
static BOOL app_icon(const char *pkg, wchar_t *path)
{
    wchar_t dir[MAX_PATH];
    if (!valid_package(pkg) || !app_folder(dir)) return FALSE;
    _snwprintf(path, MAX_PATH, L"%ls\\%hs.png", dir, pkg);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// ["com.example.app", ...]
static void serve_apps(out_t *o)
{
    static char json[16384];
    wchar_t dir[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    int n = 0;
    json[n++] = '[';
    if (app_folder(dir)) {
        _snwprintf(pattern, MAX_PATH, L"%ls\\*.png", dir);
        HANDLE find = FindFirstFileW(pattern, &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                char name[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL);
                char *dot = strrchr(name, '.');
                if (dot) *dot = 0;
                if (!valid_package(name) || n > (int)sizeof json - 200) continue;
                n += snprintf(json + n, sizeof json - n, "%s\"%s\"", n > 1 ? "," : "", name);
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }
    json[n++] = ']';
    reply(o, "200 OK", "application/json", json, n);
}
// Decodes %XX escapes (encodeURIComponent) in place
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        int hi, lo;
        if (*s == '%' && s[1] && s[2] &&
            (hi = s[1] <= '9' ? s[1] - '0' : (s[1] | 32) - 'a' + 10) >= 0 && hi < 16 &&
            (lo = s[2] <= '9' ? s[2] - '0' : (s[2] | 32) - 'a' + 10) >= 0 && lo < 16) {
            *o++ = (char)(hi << 4 | lo);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

// Keyboard of the remote: types UTF-8 text in the foreground app as characters (WM_CHAR)
static const char *type_text(const char *utf8)
{
    HWND h = target_window();
    if (!h) return "no app open";
    wchar_t w[512];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 512);
    for (int i = 0; i < n - 1; i++) PostMessageW(h, WM_CHAR, w[i], 1);
    return "ok";
}

// Standalone: serves the page and checks the PIN. External: the server does both, the mod only runs
// the requests it forwards.
static void handle(out_t *o, const char *path)
{
    BOOL standalone = g_mode == REMOTE_STANDALONE;
    if (!strcmp(path, "/") && standalone) { serve_page(o); return; }
    const char *q = strchr(path, '?');
    int len = q ? (int)(q - path) : (int)strlen(path);
    if (!q) q = path + len - 1;   // no query: q + 1 is ""
    BOOL text = len == 2 && !strncmp(path, "/t", 2), key = len == 2 && !strncmp(path, "/k", 2);
    BOOL apps = len == 5 && !strncmp(path, "/apps", 5), icon = len == 5 && !strncmp(path, "/icon", 5);
    BOOL open = len == 5 && !strncmp(path, "/open", 5);
    if (!text && !key && !apps && !icon && !open) { reply_text(o, "404 Not Found", "?"); return; }
    char c[1024], pin[sizeof g_pin];
    query_param(q + 1, text ? "s" : key ? "c" : "pkg", c, sizeof c);
    query_param(q + 1, "p", pin, sizeof pin);
    if (standalone && g_pin[0] && strcmp(pin, g_pin)) {
        Sleep(1000);   // slows down PIN guessing
        reply_text(o, "403 Forbidden", "Wrong PIN");
        return;
    }
    if (text) { url_decode(c); reply_text(o, "200 OK", type_text(c)); return; }
    if (apps) { serve_apps(o); return; }
    wchar_t ipath[MAX_PATH];
    if ((icon || open) && !app_icon(c, ipath)) { reply_text(o, "404 Not Found", "app not installed"); return; }
    if (icon) { if (!serve_file(o, ipath, "image/png")) reply_text(o, "404 Not Found", "?"); return; }
    if (open) {
        wchar_t args[160];
        _snwprintf(args, 160, L"/launch wsa://%hs", c);
        reply_text(o, "200 OK", wsa_command(args) ? "ok" : "cannot open the app");
        return;
    }
    const char *r = do_key(c);
    if (r) reply_text(o, "200 OK", r);
    else reply_text(o, "400 Bad Request", "unknown key");
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
            out_t o = { s };
            handle(&o, path);
        }
        closesocket(s);
    }
}

// ---- External remote ----
// No port is opened on the PC: the mod connects to RemoteServer (WebSocket) and runs the requests
// the server forwards. Each request is a text message "<id> <path>", with the paths of the
// standalone page (e.g. "7 /k?c=up"); each answer is a binary message
// "<id>\n<status>\n<content type>\n<body>". The connection is retried while WSA runs.

// Connects and serves requests until the connection drops; FALSE if it could not connect
static BOOL external_session(HINTERNET session, out_t *o)
{
    // WinHttpCrackUrl knows http/https only: ws:// and wss:// are read as those
    BOOL secure = !_wcsnicmp(g_server, L"wss://", 6);
    wchar_t url[600], host[256], path[512], extra[512], auth[200];
    if (!secure && _wcsnicmp(g_server, L"ws://", 5)) return FALSE;
    _snwprintf(url, 600, L"%ls%ls", secure ? L"https://" : L"http://", g_server + (secure ? 6 : 5));
    URL_COMPONENTS u = { sizeof u };
    u.lpszHostName = host;  u.dwHostNameLength = 256;
    u.lpszUrlPath = path;   u.dwUrlPathLength = 512;
    u.lpszExtraInfo = extra; u.dwExtraInfoLength = 512;
    if (!WinHttpCrackUrl(url, 0, 0, &u)) return FALSE;
    if ((int)lstrlenW(path) + lstrlenW(extra) < 511) lstrcatW(path, extra);

    BOOL connected = FALSE;
    HINTERNET c = WinHttpConnect(session, host, u.nPort, 0), ws = NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
    _snwprintf(auth, 200, L"Authorization: Bearer %ls", g_token);
    if (r && WinHttpSetOption(r, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
        WinHttpSendRequest(r, auth, (DWORD)-1, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, n = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &n, NULL);
        if (status == 101) ws = WinHttpWebSocketCompleteUpgrade(r, 0);
        else logf("external remote: the server answered %lu", status);
    }
    if (r) WinHttpCloseHandle(r);
    if (ws) {
        connected = TRUE;
        logf("external remote: connected to %ls", g_server);
        char req[2048];
        for (;;) {
            DWORD len = 0, got, err;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            do {
                if (len >= sizeof req - 1) { err = ERROR_INSUFFICIENT_BUFFER; break; }
                err = WinHttpWebSocketReceive(ws, req + len, sizeof req - 1 - len, &got, &type);
                len += got;
            } while (!err && type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);
            if (err || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                logf("external remote: disconnected (%lu)", err);
                break;
            }
            if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
            req[len] = 0;
            char *sp = strchr(req, ' ');
            if (!sp) continue;
            *sp = 0;
            o->len = snprintf(o->buf, o->cap, "%s\n", req);
            handle(o, sp + 1);
            if (WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, o->buf, o->len)) break;
        }
        WinHttpCloseHandle(ws);
    }
    if (c) WinHttpCloseHandle(c);
    return connected;
}

static DWORD WINAPI remote_external(LPVOID unused)
{
    out_t o = { INVALID_SOCKET, NULL, 0, (1 << 20) + 1024 };   // icons are up to 1 MB
    o.buf = HeapAlloc(GetProcessHeap(), 0, o.cap);
    HINTERNET session = WinHttpOpen(L"WSASmartTV", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!o.buf || !session) { logf("external remote: cannot start (%lu)", GetLastError()); return 0; }
    logf("external remote: server %ls", g_server);
    int wait = 5, failed = 0;
    for (;;) {
        if (external_session(session, &o)) { wait = 5; failed = 0; }
        else {
            if (!failed++) logf("external remote: cannot connect to %ls (%lu), retrying", g_server, GetLastError());
            wait = wait * 2 > 60 ? 60 : wait * 2;
        }
        Sleep(wait * 1000);
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

    // RemoteMode = off | standalone | external; without it (config of 0.3.x) RemotePort decides
    wchar_t pin[sizeof g_pin], mode[16];
    g_port = GetPrivateProfileIntW(L"WSASmartTV", L"RemotePort", 0, ini);
    GetPrivateProfileStringW(L"WSASmartTV", L"RemoteMode", L"", mode, 16, ini);
    GetPrivateProfileStringW(L"WSASmartTV", L"RemotePin", L"", pin, sizeof g_pin, ini);
    WideCharToMultiByte(CP_UTF8, 0, pin, -1, g_pin, sizeof g_pin, NULL, NULL);
    GetPrivateProfileStringW(L"WSASmartTV", L"RemoteServer", L"", g_server, 512, ini);
    GetPrivateProfileStringW(L"WSASmartTV", L"RemoteToken", L"", g_token, 128, ini);
    GetPrivateProfileStringW(L"WSASmartTV", L"RemoteHome", L"com.spocky.projengmenu", g_home, 128, ini);
    if (!lstrcmpiW(mode, L"external")) g_mode = g_server[0] ? REMOTE_EXTERNAL : REMOTE_OFF;
    else if (!lstrcmpiW(mode, L"off")) g_mode = REMOTE_OFF;
    else g_mode = g_port ? REMOTE_STANDALONE : REMOTE_OFF;
    if (g_mode == REMOTE_STANDALONE) CloseHandle(CreateThread(NULL, 0, remote_server, NULL, 0, NULL));
    if (g_mode == REMOTE_EXTERNAL) CloseHandle(CreateThread(NULL, 0, remote_external, NULL, 0, NULL));

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    real_GetDpiForWindow = (GetDpiForWindow_t)GetProcAddress(user32, "GetDpiForWindow");
    real_ShowWindow = (ShowWindow_t)GetProcAddress(user32, "ShowWindow");
    real_GetDpiForMonitor = (GetDpiForMonitor_t)GetProcAddress(shcore, "GetDpiForMonitor");
    real_GetKeyState = (GetKeyState_t)GetProcAddress(user32, "GetKeyState");

    HMODULE exe = GetModuleHandleW(NULL);
    void *a = patch_delay(exe, "USER32.dll", "GetDpiForWindow", 0, my_GetDpiForWindow);
    void *b = patch_delay(exe, "api-ms-win-shcore-scaling-l1-1-1.dll", "GetDpiForMonitor", 0, my_GetDpiForMonitor);
    void *c = patch_delay(exe, "USER32.dll", "ShowWindow", 0, my_ShowWindow);
    void *k = patch_delay(exe, "USER32.dll", "GetKeyState", 0, my_GetKeyState);

    // virtual gamepad for the remote OK key: XInputGetState = ordinal 2, XInputGetCapabilities = 4
    if (g_mode != REMOTE_OFF) {
        HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
        real_XInputGetState = (XInputGetState_t)GetProcAddress(xinput, (LPCSTR)2);
        real_XInputGetCapabilities = (XInputGetCapabilities_t)GetProcAddress(xinput, (LPCSTR)4);
        if (real_XInputGetState && real_XInputGetCapabilities) {
            void *gs = patch_delay(exe, "XINPUT1_4.dll", NULL, 2, my_XInputGetState);
            void *gc = patch_delay(exe, "XINPUT1_4.dll", NULL, 4, my_XInputGetCapabilities);
            g_pad_on = gs && gc;
        }
        logf("virtual gamepad: %d", g_pad_on);
    }
    logf("loaded: density=%d dpi=%d fullscreen=%d hook dpiWin=%d dpiMon=%d show=%d keyState=%d",
         density, g_dpi, g_fullscreen, a != NULL, b != NULL, c != NULL, k != NULL);
    return TRUE;
}
