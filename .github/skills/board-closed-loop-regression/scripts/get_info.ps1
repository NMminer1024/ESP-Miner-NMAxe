# One-shot baseline snapshot for the closed-loop report. Generic: prints the
# identity block plus any dotted fields the caller asks for.
param(
    [string]$Ip = $(throw 'Board IP required'),
    [string]$Path = '/api/system/info',
    [string[]]$Fields = @('identity.fwVersion', 'identity.hwModel', 'identity.appSha256')
)
function Get-Dotted($obj, [string]$path) {
    $cur = $obj
    foreach ($seg in $path.Split('.')) { $cur = $cur.$seg; if ($null -eq $cur) { return $null } }
    return $cur
}
$i = try { Invoke-RestMethod "http://$Ip$Path" -TimeoutSec 5 } catch { $null }
if (-not $i) { 'device not answering'; exit 1 }
foreach ($f in $Fields) { "{0} = {1}" -f $f, (Get-Dotted $i $f) }
