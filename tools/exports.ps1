param([string]$Path)
$b=[System.IO.File]::ReadAllBytes($Path)
function U16($o){[BitConverter]::ToUInt16($b,$o)}
function U32($o){[BitConverter]::ToUInt32($b,$o)}
$pe=U32 0x3C; $coff=$pe+4; $numSec=U16 ($coff+2); $optOff=$coff+20
$is64=(U16 $optOff) -eq 0x20b; $ddOff=if($is64){$optOff+112}else{$optOff+96}
$expRva=U32 ($ddOff); $secOff=$optOff+(U16 ($coff+16))
$secs=@(); for($i=0;$i -lt $numSec;$i++){$s=$secOff+$i*40;$secs+=[pscustomobject]@{VA=(U32 ($s+12));VSize=(U32 ($s+8));Raw=(U32 ($s+20));RSize=(U32 ($s+16))}}
function R2O($r){foreach($s in $secs){if($r -ge $s.VA -and $r -lt ($s.VA+[Math]::Max($s.VSize,$s.RSize))){return $s.Raw+($r-$s.VA)}}; -1}
function Z($o){$sb=New-Object System.Text.StringBuilder; while($b[$o] -ne 0){[void]$sb.Append([char]$b[$o]);$o++}; $sb.ToString()}
$e=R2O $expRva
$nNames=U32 ($e+24); $pNames=R2O (U32 ($e+32))
for($i=0;$i -lt $nNames;$i++){ Write-Output (Z (R2O (U32 ($pNames+$i*4)))) }
