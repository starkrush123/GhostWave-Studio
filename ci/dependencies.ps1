$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Dependency setup is allowed only in GitHub Actions.' }
New-Item -ItemType Directory -Force vendor/media, vendor/archives, ci-output | Out-Null
function Get-Archive($Url, $Name) {
    $Destination = Join-Path 'vendor/archives' $Name
    if (!(Test-Path $Destination)) { Invoke-WebRequest $Url -OutFile $Destination }
    return $Destination
}
function Expand-Source($Archive, $Destination) {
    if (Test-Path $Destination) { return }
    $Temporary = "$Destination.extract"
    New-Item -ItemType Directory -Force $Temporary | Out-Null
    Expand-Archive $Archive $Temporary
    $Root = @(Get-ChildItem $Temporary -Directory)
    if (Test-Path "$Temporary/CMakeLists.txt") { Move-Item $Temporary $Destination }
    elseif ($Root.Count -eq 1) { Move-Item $Root[0].FullName $Destination; Remove-Item $Temporary -Recurse }
    else { throw "Unexpected archive layout: $Archive" }
}
$Wx = Get-Archive 'https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.10/wxWidgets-3.2.10.zip' 'wxWidgets-3.2.10.zip'
Expand-Source $Wx 'vendor/wxWidgets'
$Json = Get-Archive 'https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.zip' 'json-3.12.0.zip'
Expand-Source $Json 'vendor/json'
$Unicode = Get-Archive 'https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v2.8.0.zip' 'utf8proc-2.8.0.zip'
Expand-Source $Unicode 'vendor/utf8proc'
git clone --depth 1 --branch v1.15.3 --filter=blob:none --sparse https://github.com/scipy/scipy.git vendor/scipy
git -C vendor/scipy sparse-checkout set scipy/_lib
git -C vendor/scipy submodule update --init --depth 1 scipy/_lib/pocketfft
Copy-Item vendor/scipy/scipy/_lib/pocketfft vendor/pocketfft -Recurse
git clone --depth 1 --branch 1.2.2 https://github.com/libsndfile/libsndfile.git vendor/libsndfile
$FfmpegName = 'ffmpeg-n8.1.3-9-g29e619e767-win64-gpl-8.1.zip'
$FfmpegBase = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-30-13-08'
$Ffmpeg = Get-Archive "$FfmpegBase/$FfmpegName" $FfmpegName
$Checksums = Get-Archive "$FfmpegBase/checksums.sha256" 'ffmpeg-checksums.sha256'
$Expected = ((Get-Content $Checksums | Where-Object { $_ -match [regex]::Escape($FfmpegName) }) -split '\s+')[0]
if (!$Expected -or (Get-FileHash $Ffmpeg -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Expected.ToLowerInvariant()) { throw 'FFmpeg checksum failed.' }
Expand-Source $Ffmpeg 'vendor/ffmpeg'
Copy-Item vendor/ffmpeg/bin/ffmpeg.exe vendor/media/ffmpeg.exe -Force
python -m pip download --only-binary=:all: -r ci/oracle-requirements.txt -d vendor/archives/wheels
python -m pip install --no-index --find-links vendor/archives/wheels -r ci/oracle-requirements.txt
$Sndfile = python -c 'import soundfile; print(soundfile._libname)'
Copy-Item $Sndfile vendor/media/libsndfile_x64.dll -Force
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
    hashes = @{}
}
Get-ChildItem vendor/archives -File -Recurse | ForEach-Object { $Record.hashes[$_.Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
Get-ChildItem vendor/media -File | ForEach-Object { $Record.hashes[$_.Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$Record | ConvertTo-Json -Depth 8 | Set-Content ci-output/dependencies.json -Encoding utf8
if (Test-Path ci/dependency-hashes.json) {
    $Frozen = Get-Content ci/dependency-hashes.json -Raw | ConvertFrom-Json -AsHashtable
    foreach ($Name in $Frozen.Keys) { if ($Record.hashes[$Name] -ne $Frozen[$Name]) { throw "Dependency hash changed: $Name" } }
}
