$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Packaging is allowed only in GitHub Actions.' }
if (!(Test-Path ci/dependency-hashes.json) -or !(Test-Path ci/reference-hashes.json)) { throw 'Freeze the first verified dependency and reference captures before creating a draft release.' }
$Native = Get-Content ci-output/native-results.json -Raw | ConvertFrom-Json
if ($Native.failures -ne 0 -or !$Native.byte_parity_required) { throw 'Native compatibility gate failed.' }
$Short = $env:GITHUB_SHA.Substring(0, 12)
$Name = "GhostWaveStudio-v2.0.0-$Short-win64-Portable"
$Stage = 'ci-output/portable'
New-Item -ItemType Directory -Force $Stage, "$Stage/licenses", ci-output/package | Out-Null
foreach ($File in @('GhostWaveStudio.exe','ghostwave-cli.exe','ffmpeg.exe','libsndfile_x64.dll')) { Copy-Item "build/bin/Release/$File" $Stage }
Copy-Item build/bin/Release/resources, build/bin/Release/locales $Stage -Recurse
Copy-Item README.txt, README_ID.txt, changelog.txt, LICENSE $Stage
Copy-Item vendor/wxWidgets/docs/licence.txt "$Stage/licenses/wxWidgets.txt"
Copy-Item vendor/json/LICENSE.MIT "$Stage/licenses/json.txt"
Copy-Item vendor/utf8proc/LICENSE.md "$Stage/licenses/utf8proc.txt"
Copy-Item vendor/libsndfile/COPYING "$Stage/licenses/libsndfile.txt"
Copy-Item vendor/pocketfft/LICENSE.md "$Stage/licenses/pocketfft.txt"
Copy-Item vendor/ffmpeg/LICENSE.txt "$Stage/licenses/ffmpeg.txt"
Copy-Item ci-output/dependencies.json "$Stage/licenses/dependencies.json"
@'
Bundled source and build provenance
wxWidgets 3.2.10: https://github.com/wxWidgets/wxWidgets/releases/tag/v3.2.10
json 3.12.0: https://github.com/nlohmann/json/tree/v3.12.0
utf8proc 2.8.0: https://github.com/JuliaStrings/utf8proc/tree/v2.8.0
PocketFFT: revision recorded in dependencies.json from SciPy 1.15.3
libsndfile: SoundFile 0.13.1 Windows wheel, upstream https://github.com/libsndfile/libsndfile
FFmpeg: https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-09-30-13-08
FFmpeg build recipes: https://github.com/BtbN/FFmpeg-Builds
FFmpeg source: https://git.ffmpeg.org/ffmpeg.git at the bundled version revision
The complete corresponding source and build recipes must accompany any public redistribution where its license requires them.
'@ | Set-Content "$Stage/licenses/SOURCES.txt" -Encoding utf8
$Manifest = [ordered]@{ application='GhostWave Studio'; version='2.0.0'; commit=$env:GITHUB_SHA; architecture='x64'; minimum_windows='Windows 10 22H2'; run_id=$env:GITHUB_RUN_ID; files=@{} }
Get-ChildItem $Stage -File -Recurse | ForEach-Object { $Relative=[IO.Path]::GetRelativePath((Resolve-Path $Stage),$_.FullName).Replace('\','/'); $Manifest.files[$Relative]=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$Manifest | ConvertTo-Json -Depth 8 | Set-Content "$Stage/manifest.json" -Encoding utf8
Copy-Item "$Stage/manifest.json" "ci-output/package/$Name.manifest.json"
Compress-Archive -Path "$Stage/*" -DestinationPath "ci-output/package/$Name.zip" -CompressionLevel Optimal
$Digest=(Get-FileHash "ci-output/package/$Name.zip" -Algorithm SHA256).Hash.ToLowerInvariant()
"$Digest  $Name.zip" | Set-Content "ci-output/package/$Name.zip.sha256" -Encoding ascii
Expand-Archive "ci-output/package/$Name.zip" ci-output/smoke
$OriginalPath=$env:PATH
try {
    $env:PATH="$env:SystemRoot/System32;$env:SystemRoot"
    & ci-output/smoke/ghostwave-cli.exe --version
    & ci-output/smoke/ghostwave-cli.exe --help
    'Hello beautiful world' | Set-Content ci-output/smoke/lyrics.txt -Encoding utf8
    & ci-output/smoke/ghostwave-cli.exe --lyrics ci-output/smoke/lyrics.txt --lyrics-out ci-output/smoke/result.txt --config-sn ci-output/native/home/smoke.sn --no-adlibs
    if (!(Test-Path ci-output/smoke/result.txt)) { throw 'Extracted portable CLI failed.' }
    & "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -NoProfile -STA -ExecutionPolicy Bypass -File ci/ui-smoke.ps1
    if ($LASTEXITCODE -ne 0) { throw 'Native accessibility smoke test failed.' }
} finally { $env:PATH=$OriginalPath }
