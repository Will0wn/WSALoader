# Installs WSALoader in place of gfxstream_backend.dll. Requires admin. Run by WSALoader-Setup.exe.
# Safe: the original DLL is replaced ONLY after the loader has passed a load test inside the WSA
# folder. If any step fails, WSA is left as it was.
# Crafted by Will0wn - https://github.com/Will0wn
param([string]$Log)   # optional: messages and errors are also appended here (read by the setup)

$ErrorActionPreference = 'Stop'
$loader = $PSScriptRoot                     # <WSA>\Custom\WSALoader
$custom = Split-Path $loader -Parent        # <WSA>\Custom
$wsa    = Split-Path $custom -Parent        # <WSA>
$orig   = Join-Path $wsa "gfxstream_backend.dll"
$real   = Join-Path $wsa "gfxstream_real.dll"
$stage  = Join-Path $wsa "wsaloader_test.dll"
$src    = Join-Path $loader "WSALoader.dll"

# the 24 functions WsaClient imports from gfxstream_backend.dll
$fns = 'stream_renderer_transfer_write_iov','stream_renderer_get_cap_set','stream_renderer_fill_caps',
       'gfxstream_set_latte_operations','stream_renderer_resource_detach_iov','stream_renderer_resource_map',
       'stream_renderer_create_blob','stream_renderer_resource_create','stream_renderer_resource_attach_iov',
       'stream_renderer_resource_unmap','stream_renderer_ctx_detach_resource','stream_renderer_ctx_attach_resource',
       'stream_renderer_context_destroy','gfxstream_backend_create_channel','stream_renderer_context_create',
       'stream_renderer_teardown','stream_renderer_init','gfxstream_set_d3d_device','gfxstream_backend_channel_op',
       'stream_renderer_resource_unref','stream_renderer_submit_cmd','gfxstream_backend_close_channel',
       'stream_renderer_platform_resource_info','stream_renderer_transfer_read_iov'

function Say($msg) {
  Write-Host $msg
  if ($Log) { Add-Content -LiteralPath $Log -Value $msg -Encoding Default }
}
function IsLoader($f) { [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($f)).Contains('gfxstream_real.') }

try {
  if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        ).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
    throw "Administrator rights are required."
  }
  if (-not (Test-Path (Join-Path $wsa "WsaClient\WsaClient.exe")) -or -not (Test-Path $orig)) {
    throw "The Custom folder must be inside the WSA folder (C:\Program Files\Windows Subsystem for Android)."
  }
  if (-not (Test-Path $src)) { throw "Missing $src" }
  if (-not (IsLoader $src)) { throw "WSALoader.dll is not a valid build." }

  Say "1) Closing WsaClient..."
  # only WsaClient loads gfxstream_backend.dll: closing it is enough (running WsaClient.exe
  # directly, outside its package, fails with error 0xc00004bc)
  Get-Process WsaClient -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 1

  Say "2) Backing up the original as gfxstream_real.dll..."
  if (IsLoader $orig) {
    if (-not (Test-Path $real)) { throw "A loader is installed but gfxstream_real.dll is missing: inconsistent state, stopping." }
    Say "   WSALoader already installed, updating."
  } else {
    Copy-Item -LiteralPath $orig -Destination $real -Force
  }

  Say "3) Testing the loader inside the WSA folder..."
  Copy-Item -LiteralPath $src -Destination $stage -Force
  $probe = @"
`$ProgressPreference = 'SilentlyContinue'
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class P {
 [DllImport("kernel32",CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryW(string p);
 [DllImport("kernel32",CharSet=CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr h,string n); }'
`$h = [P]::LoadLibraryW('$stage'); if (`$h -eq [IntPtr]::Zero) { 'LOAD_FAIL'; exit }
`$bad = @('$($fns -join "','")') | Where-Object { [P]::GetProcAddress(`$h, `$_) -eq [IntPtr]::Zero }
if (`$bad) { 'MISSING ' + (`$bad -join ',') } else { 'OK' }
"@
  $env:WSALOADER_NOMODS = '1'   # in the test process the loader loads no mods
  # -EncodedCommand: Windows PowerShell 5.1 strips the inner double quotes of a -Command argument
  $res = powershell -NoProfile -EncodedCommand ([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($probe)))
  Remove-Item Env:\WSALOADER_NOMODS
  Remove-Item -LiteralPath $stage -Force -ErrorAction SilentlyContinue
  if ($res -ne 'OK') { throw "Loader test FAILED ($res). gfxstream_backend.dll was not touched: WSA is unchanged." }
  Say "   OK: loader loaded, 24/24 functions resolved."

  Say "4) Putting WSALoader in place of gfxstream_backend.dll..."
  Copy-Item -LiteralPath $src -Destination $orig -Force

  # the Custom folder becomes writable by the user: mod configs and new mods without admin
  # (mods run inside WsaClient, which already runs as the user: no extra privilege)
  $user = (Get-CimInstance Win32_ComputerSystem).UserName
  if (-not $user) { $user = $env:USERNAME }
  Say "5) Making the Custom folder writable by $user..."
  icacls $custom /grant "$($user):(OI)(CI)M" | Out-Null

  $mods = Get-ChildItem $custom -Directory | Where-Object { $_.Name -ne 'WSALoader' -and (Test-Path (Join-Path $_.FullName "$($_.Name).dll")) }
  Say "Installed. Mods found: $(if ($mods) { ($mods.Name) -join ', ' } else { 'none' })"
  $ip = (Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway } | Select-Object -First 1).IPv4Address.IPAddress
  if ($ip) { Say "PC IP: $ip" }
} catch {
  Say "ERROR: $($_.Exception.Message)"
  exit 1
}
