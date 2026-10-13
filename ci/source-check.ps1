$ErrorActionPreference='Stop'
if (git ls-files installer/) { throw 'Downloaded packages must not be tracked.' }
if (git ls-files '*.py') { throw 'Python application files must not be tracked.' }
if (Get-Content changelog.txt -Raw | Select-String 'https?://') { throw 'Changelog must contain no download URLs.' }
foreach ($Name in @('README.txt','README_ID.txt')) {
    if (Get-Content $Name -Raw | Select-String '(?m)^#{1,6}\s|```|\[[^\]]+\]\(https?://') { throw "Expected plain text documentation: $Name" }
}
$English=Get-Content resources/ui-en.json -Raw | ConvertFrom-Json -AsHashtable
$Indonesian=Get-Content resources/ui-id.json -Raw | ConvertFrom-Json -AsHashtable
if (@(Compare-Object @($English.Keys | Sort-Object) @($Indonesian.Keys | Sort-Object)).Count) { throw 'UI language keys differ.' }
foreach ($Catalog in @($English,$Indonesian)) { foreach ($Copy in $Catalog.Values) { if ($Copy.Contains('-') -or $Copy.Contains([char]0xfffd)) { throw 'Invalid built in UI copy.' } } }
$Mapping=Get-Content tests/legacy-tests.json -Raw | ConvertFrom-Json
if ($Mapping.Count -ne 52 -or @($Mapping | Where-Object { !$_.legacy -or !$_.native -or !$_.evidence }).Count) { throw 'Historical test traceability is incomplete.' }
