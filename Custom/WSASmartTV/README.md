# WSASmartTV

Mod for **WSALoader** that makes WSA comfortable to use like a TV:

1. **Scale** - sets the Android UI density (DPI) for every app each time WSA starts, without
   re-applying it by hand through adb.
2. **Fullscreen** - every Android app opens in fullscreen.
3. **Remote** - a web page on your home network, opened in the phone browser, with a TV-remote
   layout and a PIN.

Requires WSALoader (see `Custom\WSALoader\README.md`). Settings are chosen in the setup and can be
changed later in `config.ini`. To disable the mod: rename `WSASmartTV.dll` and restart WSA.

---

## Settings - `config.ini`

```ini
[WSASmartTV]
; Android density (UI scale). 0 = same as Windows
Density=560
; 1 = every app opens in fullscreen (automatic F11), 0 = off
Fullscreen=1
; Port of the remote page. 0 = remote off
RemotePort=8580
; PIN asked by the page, digits only (empty = no PIN)
RemotePin=1234
; App opened by the Home key (Android package name)
RemoteHome=com.spocky.projengmenu
```

Put comments on their own line: after a value they would be read as part of it. Changes apply the
next time WSA starts.

---

## Phone remote

While WSA is running, open **`http://<PC IP>:8580`** (port = `RemotePort`) in the phone browser,
enter the PIN the first time (the browser remembers it) and add the page to the Home Screen: it opens
like an app. No app to download, no ADB.

Keys: arrows and OK, Back, Home, Play/Pause, volume −/mute/+. Holding arrows and volume repeats them.
The page is `remote.html`: it can be edited without recompiling.

The page exists only while WSA is running (after an Android app has been opened).

The remote **does not use ADB**: it sends keys to WsaClient's windows from the inside, so WSA's
*Developer mode* is not needed. With Developer mode on, WSA exposes ADB on the network (port `58526`)
and anyone on the local network can connect with full access: if you don't need ADB, keep it off.

---

## How it works

Hooks in the **delay-load** import table of `WsaClient.exe` (the .exe is not patched on disk):

- **Scale**
  - `GetDpiForWindow` (USER32) -> returns the DPI computed from `config.ini`.
  - `GetDpiForMonitor` (api-ms-win-shcore-scaling) -> forces the same DPI.
  - Conversion: `windows_dpi = Density * 96 / 160`. With `Density=560` -> 336 dpi.
- **Fullscreen**
  - `ShowWindow` (USER32) -> for every new top-level window whose class contains `.` (= Android
    package name) sends `WM_KEYDOWN`/`WM_KEYUP` `VK_F11` once, after ~800 ms, from a separate
    thread.
  - Splash screens (`<package>(splash)`) are skipped: in fullscreen they got stuck when the app
    closed.
- **Remote**
  - A thread serves `remote.html` on `RemotePort` (all interfaces, IPv4). The firewall lets it
    through with the rules WSA creates for `WsaClient.exe`.
  - Every key is a `GET /k?c=<key>&p=<PIN>` request; a wrong PIN -> `403` after 1 s (slows down
    guessing).
  - Arrows, OK (`Enter`), Play/Pause -> `WM_KEYDOWN`/`WM_KEYUP` to the foreground app
    window (or the topmost open one).
  - Back -> `Alt+Left` (WSA's Back), sent synchronously while a `GetKeyState` hook reports Alt as
    pressed.
  - Volume and mute -> system keys (`SendInput`): they change the Windows volume.
  - Home -> `WsaClient.exe /launch wsa://<RemoteHome>`, like the Start menu shortcuts.

Log: `%TEMP%\WSASmartTV.log` (`loaded: ...`, `new window ...`, `F11 sent ...`,
`remote listening ...`).

---

## Files

| Path | Role |
| --- | --- |
| `Custom\WSASmartTV\WSASmartTV.dll` | the mod |
| `Custom\WSASmartTV\config.ini` | settings |
| `Custom\WSASmartTV\remote.html` | the remote page |
| `Custom\WSASmartTV\README.md` | this file |

Source: `src\WSASmartTV\wsasmarttv.c`.

## To be verified

- That with `Density=560` the mouse pointer lands where you click.
- The remote with WSA's Developer mode off (so far tested only with it on; by design it should not
  be needed).

---

Crafted by [Will0wn](https://github.com/Will0wn)
