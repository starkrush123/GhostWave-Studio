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
if ((git -C vendor/scipy/scipy/_lib/pocketfft rev-parse HEAD) -ne '9367142748fcc9696a1c9e5a99b76ed9897c9daa') { throw 'PocketFFT source revision changed.' }
Copy-Item vendor/scipy/scipy/_lib/pocketfft vendor/pocketfft -Recurse
git clone --depth 1 --branch 1.2.2 https://github.com/libsndfile/libsndfile.git vendor/libsndfile
if ((git -C vendor/libsndfile rev-parse HEAD) -ne '72f6af15e8f85157bd622ed45b979025828b7001') { throw 'libsndfile header revision changed.' }
$Ffmpeg = Get-Archive 'https://github.com/FFmpeg/FFmpeg/archive/1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7.zip' 'ffmpeg-8.1.3-source.zip'
Expand-Source $Ffmpeg 'vendor/ffmpeg'
python -m pip download --only-binary=:all: -r ci/oracle-requirements.txt -d vendor/archives/wheels
python -m pip install --no-index --find-links vendor/archives/wheels -r ci/oracle-requirements.txt
$Sndfile = python -c 'import soundfile; print(soundfile._full_path)'
Copy-Item $Sndfile vendor/media/libsndfile_x64.dll -Force
