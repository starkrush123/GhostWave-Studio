GhostWave Studio 2.0.0

Native C++17 portable application for Windows 10 22H2 and Windows 11 x64.
The desktop interface uses wxWidgets native Windows controls with accessibility enabled.
No Python installation or separate FFmpeg installation is needed by the portable application.

Getting started

Extract the complete portable ZIP into a writable folder and open GhostWaveStudio.exe.
Choose your language on first use.
In Audio, choose an input, preset and format, then select Process audio.
In Lyrics, paste your text, choose a mode and select Process lyrics, then Copy result.
Detailed audio and lyric options are available in Tools, Advanced.
Tools also provides cloud credentials, artist names, slicing and acoustic comparison.
Help provides updates, changelog, support tickets and keyboard guidance.
Audio and lyric measurements describe the output and do not guarantee acceptance by another service.

Keyboard and screen readers

Tab and Shift+Tab move between controls; Ctrl+Tab switches tabs.
Ctrl+O opens audio; Ctrl+Enter processes the active tab; Escape cancels active processing.
F1 opens keyboard guidance; F2 opens the changelog.
Processing uses a worker, and controls are restored after completion or cancellation.
The CI smoke test checks native control names, keyboard focus and clean window closure.
Actual NVDA and Narrator use requires a manual Windows check before public distribution.

Profiles

Existing .sn profiles remain supported, including cloud tokens, settings and support ticket credentials.
The application uses ghostwave.sn in the current directory when present, otherwise .ghostwave.sn in your user directory.
Save and import dialogs support other .sn files.
Valid profiles receive an atomic .bak backup when saved.
A damaged profile is preserved; if a backup is recovered, export it to a new file before saving.
The inherited encrypted format uses an application key and does not protect secrets from someone who can reverse engineer the application.
Keep profiles and backups private.

Command line

Use ghostwave-cli.exe --help for all switches.
ghostwave-cli.exe --input song.wav --output result.wav --preset balanced
ghostwave-cli.exe --lyrics lyrics.txt --lyrics-out result.txt --lyrics-mode phonetic --no-adlibs
ghostwave-cli.exe --input song.wav --slice --slice-duration 22 --output chunks
ghostwave-cli.exe --input song.wav --output result.wav --audit-only
Use --config-sn FILE to select an existing profile.
The historical max, balanced and cloud CLI presets retain their historical processing behavior.

Cloud and updates

Stem separation supports Replicate, Hugging Face and a custom HTTPS endpoint.
Remote lyrics support Groq, OpenRouter, OpenAI and a custom compatible HTTPS endpoint.
Provider availability, model access and credentials depend on the external service.
Support requests are sent only when you submit a ticket or reply.
The updater checks published portable releases from starkrush123/GhostWave-Studio.
Drafts and prereleases are excluded; downloads require a matching checksum and safe archive paths.
The downloaded update opens in a new folder without replacing the running application.

Development and verification

All installation, native compilation, native tests and packaging run in GitHub Actions.
Do not install anything or compile locally; local work is limited to small source checks.
The one Windows workflow runs on pushes to main and prevents overlapping runs for a commit.
It builds with MSVC 2022, CMake, wxWidgets 3.2.10 and pinned media dependencies.
Python runs only in CI from historical commit a5e5fa6f7e26174b1fa424e7089889546cc44120 as a compatibility oracle.
The gate requires identical audio bytes, including stage results and WAV and MP3 exports.
Reference state includes the complete NumPy RandomState and Gaussian cache.
The first dependency and reference captures must be frozen before release packaging is allowed.
Passing build and compatibility gates produces a portable ZIP, checksum and source manifest in a draft release.
Manual screen reader checks remain distinct from automated structural checks.
Downloaded drafts belong in installer/, which is ignored and must never be committed.
See AGENTS.md and CONTRIBUTING.md for the full workflow.

Credits

Original project by Muhamad alfian and Technokers lab.
The repository LICENSE covers project code; bundled components retain their own licenses in licenses/.
Review the FFmpeg and libsndfile source and redistribution requirements before publishing a portable package.
