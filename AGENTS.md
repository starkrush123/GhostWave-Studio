# GhostWave Studio development rules

## Scope and compatibility

* Replace the Python application completely with native C++17 for Windows.
* Target Windows 10 22H2 and Windows 11, x64, using MSVC 2022 and CMake.
* Use wxWidgets 3.2.10 native Windows controls with accessibility enabled.
* Keep the desktop application portable; do not require a Python installation.
* Preserve audio processing, six presets, slicing, inspection, and acoustic audits.
* Preserve lyrics processing, dictionaries, Unicode handling, all modes, and audits.
* Preserve cloud stems, remote lyrics, encrypted profiles, translations, updates,
  changelog caching, support tickets, and help dialogs.
* Preserve all existing CLI switches and their actual historical behavior.
* Keep shared processing code independent of GUI dialogs for headless tests.
* The Python reference is commit a5e5fa6f7e26174b1fa424e7089889546cc44120.
* Git history provides the rollback path; do not ship a second Python application.

## Evidence first

* Use Exa MCP exclusively for external research: web_search_exa and web_fetch_exa.
* Search immediately for external, uncertain, current, niche, version specific,
  framework, package, API, implementation, or independently verifiable information.
* Fetch relevant discovered URLs with web_fetch_exa before relying on their content.
* Prefer official documentation, repositories, release notes, and specifications.
* Search snippets are discovery evidence, not sufficient implementation evidence.
* Never use native web tools, browser fetching, curl, wget, or arbitrary HTTP clients
  for research; do not silently switch providers if Exa is unavailable.
* Refine failed searches, retry failed fetches, and seek alternate primary sources.
* Verify APIs, commands, flags, versions, configuration, and limitations before use.
* Treat unresolved claims as unresolved; never invent documentation or certainty.
* Before finishing, confirm that applicable research was searched and fetched through
  Exa and that no material technical assumption remains unsupported.
* Application HTTP requests, gh repository operations, and CI dependency retrieval
  are operational actions; they are not alternative web research providers.

## Local execution and CI

* Never install packages, dependencies, toolchains, or applications locally.
* Never compile, link, or configure a native build locally, including CMake configure.
* Local work is limited to source edits and small checks using existing tools.
* Perform all compilation, native tests, dependency installation, and packaging in
  GitHub Actions on windows-2022, with pinned dependencies and recorded hashes.
* Use one automatically triggered Windows workflow on pushes to main.
* Do not add parallel tag or pull request triggers for the same commit.
* Set workflow concurrency to ghostwave-windows-${{ github.sha }} and
  cancel-in-progress: false; keep build, test, package, and release in one job.
* Do not start a second run for a SHA while its run is pending or running.
* After every commit and push, find its run with gh run list --commit FULL_SHA.
* Start gh run watch RUN_ID --exit-status in a background terminal session.
* Monitor that session to completion, inspect failures, and fix before continuing.
* A successful compiler invocation alone does not prove functional compatibility.

## Validation and data protection

* Historical Python may run only in CI as a controlled compatibility oracle.
* Freeze its input files, full NumPy RandomState including Gaussian cache,
  dependencies, libsndfile, FFmpeg, and recorded runner environment.
* Audio parity must be byte for byte, including intermediates and final exports.
* Never introduce numeric tolerances or weaken a failing compatibility gate.
* Preserve floating point precision, operation order, RNG consumption, filters,
  FFT padding, resampling, container encoding, and short remainder slice merging.
* Map historical tests to meaningful C++ checks and add failure boundary checks.
* Test mono, stereo, silence, short inputs, odd FFT sizes, invalid files, nonfinite
  samples, cancellation, and failed exports without losing existing destinations.
* Preserve the .sn header, key derivation, Fernet authentication, fields, defaults,
  path resolution, imports, exports, backups, and legacy migration behavior.
* Never overwrite corrupt profiles with defaults; preserve the original and report.
* Make writes atomic, clean temporary files, and make retries safe.
* Never log credentials or forward authorization to unrelated download hosts.
* Test cloud and support protocols using fixtures; do not send real tickets or
  messages during development without explicit authorization.

## UI and accessibility

* Keep two simple tabs, Audio and Lyrics, with detailed options in Advanced.
* Use native labeled controls, logical focus order, keyboard shortcuts, high
  contrast compatibility, accessible names and roles, and throttled progress.
* Target NVDA and Narrator; distinguish structural checks from actual manual use.
* Run processing in workers, support cancellation, and prevent callbacks after close.
* Avoid gradients, glow, glass effects, decorative icons, cards, and dummy metrics.
* Built in visible copy must contain no ASCII hyphen or replacement characters.
* Labels have at most four English words or seven Indonesian words.
* Descriptions contain exactly one sentence.
* Do not modify actual lyrics, user data, or filenames to satisfy UI copy rules.
* Describe measured audits neutrally; do not promise bypass of external services.

## Portable releases

* Use starkrush123/GhostWave-Studio for updates and release operations.
* Check published portable ZIP releases only; drafts are not application updates.
* Package EXEs, required media binaries, resources, documentation, and licenses.
* Create a portable ZIP, SHA256 checksum, and a manifest identifying the full SHA.
* Upload only after all required gates pass, to a draft release; never publish it.
* Use an unambiguous tag such as v2.0.0-cpp.SHORT_SHA and include SHA in assets.
* Download the verified draft assets with gh into installer/ and verify their hashes.
* installer/ must always be ignored and must never appear in tracked files.
* Keep README and CONTRIBUTING consistent with these rules and report untested
  manual accessibility or external provider behavior honestly.
