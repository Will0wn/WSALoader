# WSALoader

Mod loader for **Windows Subsystem for Android**: it makes `WsaClient.exe` load the mod DLLs placed
in `Custom\`. Mods run **inside** WsaClient (no extra process, no background service) and start
and stop together with WSA. `WsaClient.exe` is never patched on disk.

Not affiliated with Microsoft. Since March 2025 WSA is no longer distributed through the Microsoft
Store: WSALoader is meant for existing installs and community builds (e.g. WSABuilds).

---

## Install

Run **`WSALoader-Setup-<version>.exe`** (asks for administrator rights):

1. pick the folder where WSA is installed (default `C:\Program Files\Windows Subsystem for Android`;
   the setup checks that it contains `WsaClient\WsaClient.exe`);
2. pick the mods to install (WSASmartTV is included) and their settings;
3. the setup closes WSA, copies the files into `<WSA>\Custom\` and activates the loader.

Activation (`install.ps1`):

1. backs up the original `gfxstream_backend.dll` as `gfxstream_real.dll`;
2. test-loads the loader inside the WSA folder - if the test fails it stops and **changes nothing**;
3. puts `WSALoader.dll` in place of `gfxstream_backend.dll`;
4. makes the `Custom` folder writable by the user (mod settings and new mods without administrator
   rights: mods already run as the user, inside WsaClient).

Then start an Android app: `%TEMP%\WSALoader.log` lists the loaded mods. Running the setup again
updates WSALoader and keeps the mod settings.

## Uninstall

**Settings > Apps > WSALoader > Uninstall**: closes WSA, puts the original `gfxstream_backend.dll`
back and removes the files. Apps installed in WSA are not touched.

Manual removal, if the uninstaller cannot run: from an administrator terminal run
`WsaClient.exe /shutdown`, then delete `gfxstream_backend.dll` (the loader) and rename
`gfxstream_real.dll` -> `gfxstream_backend.dll`.

---

## Mods

A mod is a folder **`Custom\<Name>\`** that contains **`<Name>.dll`** (same name as the folder).
Other folders, and the `WSALoader` folder, are ignored.

- **Add:** copy the mod folder into `Custom\` and restart WSA.
- **Disable:** rename the DLL (e.g. `<Name>.dll.off`) and restart WSA.
- **Order:** alphabetical by folder name.

Included mod: **WSASmartTV** (Android scale, fullscreen apps, phone remote) - see
`Custom\WSASmartTV\README.md`.

### Writing a mod

A mod is any Windows x64 DLL: its `DllMain` runs inside `WsaClient.exe` when WSA starts.

- `DllMain` runs under the *loader lock*: do only the minimum there (read settings, install hooks)
  and start a thread for anything else (servers, waits, long I/O).
- Keep settings and resources in the mod folder: get its path with `GetModuleFileNameW(hinstDLL, ...)`.
- The mod's own dependencies are searched in its folder first.
- To change WsaClient's behaviour, WSASmartTV hooks the *delay-load* import table of
  `WsaClient.exe` (`patch_delay` in `src\WSASmartTV\wsasmarttv.c`): a good starting point.
- Log to your own file, e.g. `%TEMP%\<Name>.log`.

---

## How it hooks in

`WsaClient.exe` loads several DLLs at startup. One of them is replaced by the loader: it exports the
same symbols as the original DLL, forwards them to the real one and loads the mods in its own
`DllMain`.

The DLL used is **`gfxstream_backend.dll`** (in the WSA root). It is the only DLL imported by
`WsaClient.exe` that does **not** also exist in `System32`: it ships only with WSA, so the local copy
always wins in the Windows DLL search. The loader:

- loads `gfxstream_real.dll` in `DllMain` by **full path** with `LOAD_WITH_ALTERED_SEARCH_PATH` (its
  folder is not in the process search path, and `gfxstream_real.dll` depends on `WSACodecs.dll`,
  which lives there);
- exports the **24 functions imported by WsaClient** as `jmp` stubs to the real DLL (indirect jump:
  arguments and calling convention pass through untouched);
- forwards the **other 377 exports** with forwarders in the `.def` (they are resolved only through
  `GetProcAddress`, when `gfxstream_real.dll` is already loaded);
- then loads the mods from `Custom\`. With the environment variable `WSALOADER_NOMODS` set it loads
  no mods (used by the install test).

Why not plain forwarders: forwarders are resolved before `DllMain` by searching
`gfxstream_real.dll` in the search path, where it is not found -> WSA does not start (error 126).

Rejected: **`icu.dll`** (in `WsaClient\`) - it also exists in `System32`, so the local copy is not
preferred. The same hook point is used by *WSAPatch* (WSA on Windows 10), which edits the import
table of an `icu.dll` taken from Windows 11.

---

## Files

| Path | Role |
| --- | --- |
| `Custom\WSALoader\WSALoader.dll` | the loader, copied to the WSA root by the setup |
| `Custom\WSALoader\install.ps1` · `uninstall.ps1` | activation / restore, run by the setup and the uninstaller |
| `Custom\WSALoader\README.md` | this file |
| WSA root, once installed: `gfxstream_backend.dll` (loader) + `gfxstream_real.dll` (original) | |

## Source and build

`src\WSALoader\wsaloader.c` + `wsaloader.def`, `src\WSASmartTV\wsasmarttv.c`,
`installer\WSALoader.iss` (Inno Setup 7), `tools\` (build script, PE parsers). Compiler: Zig
(portable, inside the project folder).

```
pwsh -NoProfile -File tools\build.ps1    # builds the DLLs into Custom\ and the setup into installer\Output\
```

The 401 exports are listed with `tools\exports.ps1` (PE parser) and written to `wsaloader.def`; the
24 functions imported by WsaClient with `tools\imports.ps1`.

---

Crafted by [Will0wn](https://github.com/Will0wn)
