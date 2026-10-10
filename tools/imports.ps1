param([string]$Path)
$b = [System.IO.File]::ReadAllBytes($Path)
function U16($o){[BitConverter]::ToUInt16($b,$o)}
function U32($o){[BitConverter]::ToUInt32($b,$o)}
$peoff = U32 0x3C
if ([System.Text.Encoding]::ASCII.GetString($b,$peoff,4) -ne "PE`0`0"){throw "not PE"}
$coff = $peoff + 4
$numSec = U16 ($coff+2)
$optOff = $coff + 20
$magic = U16 $optOff
$is64 = $magic -eq 0x20b
$ddOff = if($is64){$optOff+112}else{$optOff+96}
$impRva = U32 ($ddOff + 1*8)
$delayRva = U32 ($ddOff + 13*8)
$secOff = $optOff + (U16 ($coff+16))
$secs = @()
for($i=0;$i -lt $numSec;$i++){
  $s = $secOff + $i*40
  $secs += [pscustomobject]@{ VA=(U32 ($s+12)); VSize=(U32 ($s+8)); Raw=(U32 ($s+20)); RSize=(U32 ($s+16)) }
}
function RvaToOff($rva){
  foreach($s in $secs){ if($rva -ge $s.VA -and $rva -lt ($s.VA + [Math]::Max($s.VSize,$s.RSize))){ return $s.Raw + ($rva - $s.VA) } }
  return -1
}
function AscZ($off){
  $sb=New-Object System.Text.StringBuilder
  while($b[$off] -ne 0){ [void]$sb.Append([char]$b[$off]); $off++ }
  $sb.ToString()
}
function CountThunks($rva){
  $o = RvaToOff $rva
  if($o -lt 0){return -1}
  $n=0
  while($true){
    $v = if($is64){[BitConverter]::ToUInt64($b,$o)}else{U32 $o}
    if($v -eq 0){break}
    $n++; $o += $(if($is64){8}else{4})
  }
  $n
}
Write-Output "=== NORMAL IMPORTS ==="
if($impRva){
  $o = RvaToOff $impRva
  while($true){
    $OriginalFirstThunk = U32 $o
    $Name = U32 ($o+12)
    $FirstThunk = U32 ($o+16)
    if($OriginalFirstThunk -eq 0 -and $Name -eq 0 -and $FirstThunk -eq 0){break}
    $dll = AscZ (RvaToOff $Name)
    $intRva = $(if($OriginalFirstThunk){$OriginalFirstThunk}else{$FirstThunk})
    $cnt = CountThunks $intRva
    Write-Output ("{0,4}  {1}" -f $cnt,$dll)
    $o += 20
  }
}
Write-Output ""
Write-Output "=== DELAY IMPORTS ==="
if($delayRva){
  $o = RvaToOff $delayRva
  while($true){
    $DllNameRVA = U32 ($o+4)
    $INT = U32 ($o+16)
    if($DllNameRVA -eq 0){break}
    $dll = AscZ (RvaToOff $DllNameRVA)
    $cnt = CountThunks $INT
    Write-Output ("{0,4}  {1}" -f $cnt,$dll)
    $o += 32
  }
}
