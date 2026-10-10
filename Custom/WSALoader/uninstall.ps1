# Uninstalls WSALoader: puts the original gfxstream_backend.dll back. Requires admin.
# Run by the WSALoader uninstaller (Settings > Apps).
# Crafted by Will0wn - https://github.com/Will0wn
$ErrorActionPreference = 'Stop'
$wsa  = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$orig = Join-Path $wsa "gfxstream_backend.dll"
$real = Join-Path $wsa "gfxstream_real.dll"

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
      ).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
  throw "Administrator rights are required."
}
if (-not (Test-Path $real)) { Write-Host "WSALoader is not installed: nothing to do."; exit }

Write-Host "1) Closing WsaClient..."
# only WsaClient loads gfxstream_backend.dll: closing it is enough (running WsaClient.exe
# directly, outside its package, fails with error 0xc00004bc)
Get-Process WsaClient -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

Write-Host "2) Restoring the original gfxstream_backend.dll..."
Copy-Item -LiteralPath $real -Destination $orig -Force
Remove-Item -LiteralPath $real -Force

Write-Host "Uninstalled ($([int]((Get-Item $orig).Length/1KB)) KB). WSA starts without mods."
