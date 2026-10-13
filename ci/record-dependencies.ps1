$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$true
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Dependency verification is allowed only in GitHub Actions.' }
$Filters = & vendor/media/ffmpeg.exe -hide_banner -filters 2>&1 | Out-String
$Formats = & vendor/media/ffmpeg.exe -hide_banner -formats 2>&1 | Out-String
if ($Filters -notmatch 'rubberband' -or $Filters -notmatch 'vibrato' -or $Formats -notmatch 'chromaprint') { throw 'Required FFmpeg capabilities are missing.' }
$Record = [ordered]@{
    python = (python --version | Out-String).Trim()
    cmake = (cmake --version | Select-Object -First 1)
    pocketfft_commit = (git -C vendor/scipy/scipy/_lib/pocketfft rev-parse HEAD)
    runner_image = $env:ImageVersion
    runner_os = $env:RUNNER_OS
    ffmpeg = (& vendor/media/ffmpeg.exe -version | Select-Object -First 1)
    media_source_commit = '1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7'
    media_packages = @(Get-Content vendor/media-packages.txt)
    hashes = @{}
}
Get-ChildItem vendor/archives -File -Recurse | ForEach-Object { $Record.hashes[$_.Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
Get-ChildItem vendor/media -File | ForEach-Object { $Record.hashes[$_.Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$Record.hashes['ffmpeg-vibrato.patch']=(Get-FileHash ci/ffmpeg-vibrato.patch -Algorithm SHA256).Hash.ToLowerInvariant()
$Record.hashes['media-packages.txt']=(Get-FileHash vendor/media-packages.txt -Algorithm SHA256).Hash.ToLowerInvariant()
$Record | ConvertTo-Json -Depth 8 | Set-Content ci-output/dependencies.json -Encoding utf8
if (Test-Path ci/dependency-hashes.json) {
    $Frozen = Get-Content ci/dependency-hashes.json -Raw | ConvertFrom-Json -AsHashtable
    foreach ($Name in $Frozen.Keys) { if ($Record.hashes[$Name] -ne $Frozen[$Name]) { throw "Dependency hash changed: $Name" } }
}
