# Builds WSALoader and WSASmartTV into Custom\ (the folder packaged by the setup).
#   pwsh -NoProfile -File tools\build.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$zig  = Join-Path $root "zig-x86_64-windows-0.17.0\zig.exe"

function Build($out, [string[]]$zigArgs) {
  & $zig cc -target x86_64-windows-gnu -shared -O2 -Wall -o $out @zigArgs
  if ($LASTEXITCODE) { throw "build failed: $out" }
  # zig leaves .lib and .pdb next to the DLL: not needed
  Remove-Item ([IO.Path]::ChangeExtension($out, ".lib")), ([IO.Path]::ChangeExtension($out, ".pdb")) -ErrorAction SilentlyContinue
  Write-Host "ok  $out"
}

Build "$root\Custom\WSALoader\WSALoader.dll" @("$root\src\WSALoader\wsaloader.c", "$root\src\WSALoader\wsaloader.def")
Build "$root\Custom\WSASmartTV\WSASmartTV.dll" @("$root\src\WSASmartTV\wsasmarttv.c", "-luser32", "-lws2_32", "-lwinhttp")

# Setup (Inno Setup 7): installer\Output\WSALoader-Setup-<version>.exe
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe", "$env:ProgramFiles\Inno Setup 7\ISCC.exe") |
  Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { Write-Host "Inno Setup 7 not found: setup not built"; return }
& $iscc /Q "$root\installer\WSALoader.iss"
if ($LASTEXITCODE) { throw "setup build failed" }
Write-Host "ok  $(Get-ChildItem "$root\installer\Output\*.exe" | Sort-Object LastWriteTime | Select-Object -Last 1)"
