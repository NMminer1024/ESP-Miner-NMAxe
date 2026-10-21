# One-shot baseline snapshot for the closed-loop report.
param([string]$Ip = $(throw 'Ip required'))
$i = try { Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 5 } catch { $null }
if (-not $i) { 'device not answering'; exit 1 }
"fw=$($i.identity.fwVersion) model=$($i.identity.hwModel) sha=$($i.identity.appSha256)"
"hr=$([math]::Round($i.miner.hashRate,1)) asic=$($i.asic.model)x$($i.asic.count) freqReq=$($i.asic.freqReq) vcoreReq=$($i.asic.vcoreReq) vcore=$($i.asic.vcoreReal)"
"temp=$([math]::Round($i.temps.asic,1)) uptime=$($i.miner.uptimeSeconds) acc=$($i.miner.sAccepted) rej=$($i.miner.sRejected) heap=$($i.miner.freeHeap)"
