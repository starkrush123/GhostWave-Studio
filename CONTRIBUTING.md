# Contributing

Read [AGENTS.md](AGENTS.md) before editing. Research externally verifiable implementation details with Exa search and fetch, using official sources.

Do not install dependencies, configure CMake or compile locally. Use existing tools only for small source checks. The Windows GitHub Actions job installs pinned dependencies, captures the historical Python oracle, builds the C++ application, runs byte comparisons and checks the extracted portable UI.

Keep changes on main within the authorized workflow. Commit and push once, find the exact SHA with `gh run list --commit FULL_SHA`, and start `gh run watch RUN_ID --exit-status` in a background terminal. Wait for completion and inspect failed logs before another fix commit. Never dispatch a second run for the same SHA. The workflow uses only the main push trigger and one Windows job.

Compatibility references come from commit `a5e5fa6f7e26174b1fa424e7089889546cc44120`, with Python 3.12.10 and pinned oracle dependencies. The controlled environment disables selected CPU kernels and retains the entire NumPy RandomState. Never add tolerances or update reference hashes to make a native mismatch pass. Freeze the first reference and dependency capture only after checking its provenance and repeatability.

Do not submit real support tickets in tests. Use fixture transports for provider and support contracts. Preserve user profiles and existing output files on failure. Verify encrypted profile compatibility in both directions.

A passing workflow packages only portable application files and creates a draft release tied to the full source SHA. Keep it unpublished. Download its assets using `gh release download TAG --dir installer/`, then verify the SHA256 checksum. `git ls-files installer/` must be empty. Drafts are excluded from the application's updater.

Automated UI Automation checks do not replace manual NVDA and Narrator checks. Check both tabs, menu shortcuts, advanced dialogs, read only results, cancellation and clean closure on Windows. Record what actually passed without claiming untested accessibility or external provider availability.
