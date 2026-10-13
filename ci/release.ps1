$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$true
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Draft uploads are allowed only in GitHub Actions.' }
if (!(Get-Content ci-output/ui-results.json -Raw | ConvertFrom-Json).passed) { throw 'Accessibility gate failed.' }
if ((Get-Content ci-output/native-results.json -Raw | ConvertFrom-Json).failures -ne 0) { throw 'Compatibility gate failed.' }
$Tag="v2.0.0-cpp.$($env:GITHUB_SHA.Substring(0,12))"
$Repo='starkrush123/GhostWave-Studio'
$Notes=@"
Native C++17 Windows x64 portable application with wxWidgets controls.
Source commit: $env:GITHUB_SHA
Build run: https://github.com/$Repo/actions/runs/$env:GITHUB_RUN_ID
The Windows build, historical compatibility checks and UI Automation smoke test passed.
Manual NVDA and Narrator testing remains pending.
This release is a draft and must remain unpublished.
"@
$Notes | Set-Content ci-output/release-notes.md -Encoding utf8
$Assets=@(Get-ChildItem ci-output/package -File | ForEach-Object { $_.FullName })
gh release create $Tag @Assets --repo $Repo --draft --target $env:GITHUB_SHA --title "GhostWave Studio 2.0.0 C++ $($env:GITHUB_SHA.Substring(0,12))" --notes-file ci-output/release-notes.md
$Release=gh release view $Tag --repo $Repo --json isDraft,targetCommitish | ConvertFrom-Json
if (!$Release.isDraft -or $Release.targetCommitish -ne $env:GITHUB_SHA) { throw 'Draft identity check failed.' }
