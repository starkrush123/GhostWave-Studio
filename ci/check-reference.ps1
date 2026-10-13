$ErrorActionPreference='Stop'
$Frozen=Get-Content ci/reference-hashes.json -Raw | ConvertFrom-Json -AsHashtable
$Current=Get-Content ci-output/reference-hashes.json -Raw | ConvertFrom-Json -AsHashtable
$Changes=@($Frozen.Keys | Where-Object { $Current[$_] -ne $Frozen[$_] })
$Missing=@($Current.Keys | Where-Object { !$Frozen.ContainsKey($_) })
$Report=@{passed=($Changes.Count -eq 0 -and $Missing.Count -eq 0); changed=$Changes; unfrozen=$Missing}
$Report | ConvertTo-Json -Depth 4 | Set-Content ci-output/reference-verification.json -Encoding utf8
if (!$Report.passed) { throw "Historical reference is not repeatable or fully frozen: $($Changes -join ', '); unfrozen: $($Missing -join ', ')" }
