# WSASmartTV

Mod for **WSALoader** that makes WSA comfortable to use like a TV:

1. **Scale** - sets the Android UI density (DPI) for every app each time WSA starts, without
   re-applying it by hand through adb.
2. **Fullscreen** - every Android app opens in fullscreen.
3. **Remote** - a web page on your home network, opened in the phone browser, with a TV-remote
   layout and a PIN. Served by this PC (standalone) or by your own server (external).

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
; Phone remote: standalone, external or off
RemoteMode=standalone
; Standalone: port of the remote page
RemotePort=8580
; Standalone: PIN asked by the page, digits only (empty = no PIN)
RemotePin=1234
; External: address of your remote server (ws:// or wss://)
RemoteServer=
; External: sent to the server as "Authorization: Bearer <token>"
RemoteToken=
; App opened by the Home key (Android package name)
RemoteHome=com.spocky.projengmenu
```

Without `RemoteMode` (a `config.ini` from 0.3.x), `RemotePort=0` means off and any other port
means standalone.

Put comments on their own line: after a value they would be read as part of it. Changes apply the
next time WSA starts.

---

## Phone remote

The remote has two modes, chosen in the setup (`RemoteMode`):

- **Standalone** - this PC serves the page on `RemotePort`, protected by the PIN. Described below.
- **External** - no port is opened on this PC: while WSA runs, the mod connects to your own server
  and runs the keys it forwards. The page is served by the server, which can also control other
  devices from the same remote. See [External remote](#external-remote).

While WSA is running, open **`http://<PC IP>:8580`** (port = `RemotePort`) in the phone browser,
enter the PIN the first time (the browser remembers it) and add the page to the Home Screen: it opens
like an app. No app to download, no ADB.

Keys: arrows and OK, Back, Home, keyboard, volume down/mute/up. Holding arrows and volume repeats them.
The keyboard button opens a text field: what you type is typed in the Android app (Enter sends
Enter). The X button (top left) closes the current app like its window X button: WSA ends it, so
it starts from scratch when opened again. The row at the bottom shows the installed apps: tap one
to open it (Android TV-only apps do not open from there yet, see the main README). The power
button (top right) turns WSA off after a confirmation: the remote stops with it, until WSA is
started again on the PC.
The page is `remote.html`: it can be edited without recompiling.

The page exists only while WSA is running (after an Android app has been opened).

The remote **does not use ADB**: it sends keys to WsaClient's windows from the inside, so WSA's
*Developer mode* is not needed. With Developer mode on, WSA exposes ADB on the network (port `58526`)
and anyone on the local network can connect with full access: if you don't need ADB, keep it off.

---

## External remote

For a remote that lives on an always-on machine (a home server, a NAS, a Raspberry Pi) instead of
on the PC. WSASmartTV provides the WSA side only: the server is yours to write, and it can serve
`remote.html` as it is.

Settings:

```ini
RemoteMode=external
RemoteServer=ws://192.168.1.10:8580/wsa
RemoteToken=a-long-random-secret
```

While WSA is running the mod opens a WebSocket connection to `RemoteServer` (`ws://`, or `wss://`
with a certificate Windows trusts), sending the header `Authorization: Bearer <RemoteToken>`. If
the connection drops it retries after 5 seconds; while the server cannot be reached it waits longer
each time, up to 1 minute. When WSA is
turned off the connection closes: that is how the server knows WSA is off.

Protocol, on that connection:

- **Server -> WSA**, a text message: `<id> <path>`. The paths are the ones `remote.html` requests
  in standalone mode, so the server can forward them unchanged; the PIN parameter is ignored.
  - `/k?c=<key>` - a key: `up`, `down`, `left`, `right`, `ok`, `enter`, `backspace`, `back`,
    `home`, `close`, `power`, `volup`, `voldown`, `mute`, `ping`
  - `/t?s=<text>` - types text (URL-encoded UTF-8)
  - `/apps` - installed apps, as a JSON array of package names
  - `/icon?pkg=<package>` - icon of an installed app (PNG)
  - `/open?pkg=<package>` - opens an installed app
- **WSA -> server**, a binary message: `<id>\n<status>\n<content type>\n<body>`, for example
  `7\n200 OK\ntext/plain; charset=utf-8\nok`. `<id>` is the one of the request, any text without
  spaces.

Requests are run one at a time, in order. The server is responsible for who may use the remote: the
mod trusts whatever arrives on the connection, so keep the token secret.

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
  - Standalone: a thread serves `remote.html` on `RemotePort` (all interfaces, IPv4). The firewall
    lets it through with the rules WSA creates for `WsaClient.exe`.
  - External: a thread keeps the WebSocket connection to `RemoteServer` (WinHTTP) and passes each
    request to the same code that answers the standalone page.
  - Every key is a `GET /k?c=<key>&p=<PIN>` request; a wrong PIN -> `403` after 1 s (slows down
    guessing).
  - Arrows -> `WM_KEYDOWN`/`WM_KEYUP` to the foreground app window (or the topmost open one).
  - OK -> button A of a virtual Xbox controller. WSA forwards XInput controllers to Android's
    `virtual_gamepad`, where A falls back to `DPAD_CENTER` (`Generic.kcm`): the confirm key that
    Android TV players expect (e.g. Crunchyroll's seek bar). No WSA keyboard key produces it
    (`virtual_keyboard.kl`). The mod hooks `XInputGetState` / `XInputGetCapabilities`, reports a
    controller in slot 3 and holds A until WsaClient has read it (it polls every few ms).
  - Back -> `Alt+Left` (WSA's Back), sent synchronously while a `GetKeyState` hook reports Alt as
    pressed.
  - Volume and mute -> system keys (`SendInput`): they change the Windows volume.
  - Home -> `WsaClient.exe /launch wsa://<RemoteHome>`, like the Start menu shortcuts.
  - Power -> `WsaClient.exe /shutdown`, half a second after answering the page.
  - Keyboard -> `GET /t?s=<text>&p=<PIN>`: the text is posted as `WM_CHAR` to the app window;
    Backspace and Enter as keys.
  - Close app (X) -> `WM_CLOSE` to the app window (`SC_CLOSE` is ignored in fullscreen).
  - App row -> the icons WSA keeps in its LocalState folder (`<package>.png`); a tap runs
    `WsaClient.exe /launch wsa://<package>`. Only package names made of letters, digits, dots and
    underscores, and only installed apps, are accepted.

Log: `%TEMP%\WSASmartTV.log` (`loaded: ...`, `new window ...`, `F11 sent ...`,
`remote listening ...`, `external remote: ...`).

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

---

Crafted by [Will0wn](https://github.com/Will0wn)
