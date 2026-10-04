# CrossPoint Reader Development Guide

## Repository Identity and Scope

- Active repository: `C:\Codex\crosspoint-reader`
- Main development branch: `korean-develop`
- This is a personal Korean-focused CrossPoint fork for the XTEINK X3/X4 workflow. Use upstream as a reference, not as a compatibility requirement, unless the task explicitly targets an upstream change.
- Normal book and reader-font I/O requires an inserted SD card. Reader/body fonts, including Korean/CJK fonts, are loaded from SD. Do not add a large built-in reader-font fallback solely for SD-less reading.
- Keep changes within the requested behavior and subsystem. Report useful out-of-scope findings without implementing them.

## Agent Working Rules

- Verify current source and configuration before relying on a path, API, build flag, cache format, or architectural detail in this guide. For code proposals and reviews, cite the relevant source path and line numbers.
- Do not assume an ESP-IDF, Arduino, or FreeInk SDK API exists. Check `freeink-sdk/` or the official SDK documentation when availability is uncertain.
- Explain the technical mechanism behind memory or performance claims. Use build reports and live measurements for the selected device; do not assume a fixed free-heap budget.
- Justify new heap allocations and explain why a smaller or reusable buffer is unsuitable. Keep allocations bounded and reuse buffers in repeated paths.
- Surface requirements conflicts when the alternatives change observable behavior. Resolve routine implementation details only when they stay within the requested scope.
- User-facing strings must use the `tr()` translation mechanism. Use `LOG_INF`, `LOG_DBG`, or `LOG_ERR` for logging.

## Windows, Build, and Formatting

Use PowerShell for repository, PlatformIO, Python, and file operations. The configured PlatformIO CLI on this machine is:

```powershell
& "C:\Codex\.platformio-clean\venv\Scripts\pio.exe" run -e default
```

`default` is the ESP32-C3 environment used for the X3/X4 build. `platformio.ini` also defines ESP32-S3 environments; select and verify the environment before applying board-specific assumptions. The configured platform is pioarduino `55.03.311`, and the firmware uses `-std=gnu++2a` with exceptions disabled. Treat `platformio.ini` as the source of truth for current flags and targets.

The repository formatting entry point is the Bash wrapper. Run it from PowerShell with clang-format 21 or newer available to Git for Windows Bash:

```powershell
& "C:\Program Files\Git\bin\bash.exe" -c 'export PATH=/c/Codex/.build-tools/venv/Lib/site-packages/clang_format/data/bin:/usr/bin:$PATH; ./bin/clang-format-fix -g'
```

Do not invoke or probe `clang-format` directly. If the wrapper or its dependencies are unavailable, report that condition rather than substituting another formatter.

## Device and Resource Constraints

- The ESP32-C3 has no PSRAM. The X4 800x480 monochrome display uses a 48,000-byte framebuffer; the current firmware configuration enables single-buffer mode. Check the selected environment and renderer dimensions before relying on those figures.
- Use `renderer.getScreenWidth()`, `renderer.getScreenHeight()`, and oriented viewable bounds for layout. Do not hardcode panel dimensions in general UI logic.
- Use `MappedInputManager::Button` logical buttons in activities. Do not use raw HAL button indices outside the button-remapping activity.
- Use `HalDisplay`, `HalGPIO`, and `HalStorage` for their hardware services. Use `HalFile` for all SD access. Do not call the wrapped SDK, SdFat, `SDCardManager`, or raw file APIs directly; `HalStorage` serializes access.
- Keep large buffers off task stacks. Avoid repeated allocation/free in loops, and reserve vectors before append loops when the size can be estimated.
- Use `makeUniqueNoThrow` or another checked non-throwing allocation path for fallible buffers. Check allocation results and log before returning an error. Do not use bare `new` for an allocation that can fail.
- Use `constexpr` for compile-time constants, and avoid `std::string` or Arduino `String` in hot paths. A `std::string_view` is not null-terminated: copy or format it before passing data to a C API.
- Use `memcpy` for potentially unaligned multi-byte reads from byte buffers on ESP32-C3. ISR handlers and data they access must follow the platform's IRAM/DRAM rules; never take a task mutex from an ISR.
- Use RAII for resource cleanup. Free activity-owned resources and stop FreeRTOS tasks in `onExit()` before activity destruction.

## Current TXT Reader Contract

- TXT pagination is on demand. `TxtPageReader` reads a bounded window beginning at a requested source offset; it does not build or persist a full-file page index. Keep page navigation and rendering independent of a whole-file scan.
- Navigation and resume use exact source-byte page-start offsets. The in-memory `TxtPageHistory` is a bounded ring; progress persistence keeps the current offset and at most three previous page starts for backward navigation after reopening. Preserve the visible source offset when layout changes invalidate page history or display numbering.
- TXT page numbers and total page count are estimates based on observed page sizes and remaining file bytes. Present them as approximate, not as a precomputed exact count.
- Keep TXT menu and settings visibility behind `ReaderMenuCapabilities` and `TextSettingsCapabilities`. The current TXT menu exposes Night Mode, Rotate Screen, Screenshot, Display QR, Go Home, and Text Settings. TXT layout controls are Line Spacing, Alignment, and Screen Margin; alignment choices are Left, Center, and Right. TXT style controls are Hyphenation and Anti-Aliasing.
- The TXT Hyphenation control applies Hangul/Korean line-end splitting to eligible overflowing words. Do not restore English pattern-based TXT hyphenation, TXT Justify, paragraph spacing, or paragraph indentation.
- Recent Books stores TXT book metadata, not TXT reading progress. Keep TXT page or percentage progress out of Recent Books; resume state belongs to the TXT progress reader path.
- Current host-side TXT coverage is in `test/txt_on_demand/`. Do not refer to removed TXT indexing, cache/index-format, or legacy pagination test suites as current.

## Project Structure and UI Conventions

- `src/activities/`: activity lifecycle and screens.
- `lib/Txt/` and `lib/Epub/`: TXT and EPUB reading engines.
- `lib/hal/`: hardware abstraction layer.
- `lib/I18n/`: translations and generated string tables.
- `freeink-sdk/`: low-level SDK dependencies; application code should use the HAL where one exists.
- `.crosspoint/` on the SD card holds application state and reader data. Verify the current owner and format in source before changing serialized data or cache invalidation behavior.
- Use `GUI`/`UITheme` and the shared FreeInkUI components for common screen chrome. Keep orientation behavior in renderer-aware layout code.
- Use `#pragma once` in headers, project naming conventions, and short comments that explain a non-obvious mechanism or special case.
- Prefer `LOG_ERR` plus a recoverable return for expected failures. Reserve assertions or restart behavior for states that the surrounding recovery path cannot handle.

## Generated Files and Persistent Formats

- Edit HTML templates, not generated `*.generated.h` files. `scripts/build_html.py` regenerates the headers during the PlatformIO build.
- Edit translation YAML under `lib/I18n/translations/`, not generated `I18nKeys.h`, `I18nStrings.h`, or `I18nStrings.cpp`; `scripts/gen_i18n.py` regenerates those files.
- Treat the active implementation and format documentation as authoritative for persisted data. Do not carry old TXT cache/index schemas or hardcoded historical version numbers into new work. Update the relevant format version and documentation when a serialized structure changes.
- `platformio.local.ini` is for machine-local PlatformIO overrides and is ignored by Git. Do not commit local ports, credentials, or other machine-specific settings.

## Tests and Verification

Host-side C++ tests are GoogleTest suites under `test/<suite>/`, registered in `test/CMakeLists.txt`. The current TXT suite is `txt_on_demand`; `korean_line_breaking` is a separate suite. Follow `test/README` for the current workflow:

```powershell
cmake -S test -B build/test
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
```

GoogleTest is fetched by CMake on the first configure. CI configuration is in `.github/workflows/ci.yml`. For firmware changes, build the selected PlatformIO environment and inspect its memory report; hardware-only behavior requires validation on the target device.

## Git and External Collaboration

Before Git work, check the current branch, remotes, and working tree:

```powershell
git branch --show-current
git remote -v
git status --short
```

- Do not switch branches or reset, stash, clean, or move worktrees unless the active task asks for it. Main development proceeds on `korean-develop`; verify the requested comparison base for other work.
- Commit only when explicitly requested. Never push, open or merge a PR, or deploy firmware without explicit authorization. Inspect the configured remotes again before an approved push; do not infer a remote name or target.
- Do not add assistant self-attribution or generated-by trailers to commits.
- CrossTwins Slack, iCloud, and Notion workflows are not automatic. Do not post messages or upload reports/builds unless the active request asks. Archive reusable prompts to Notion only when explicitly requested. External messages do not authorize code changes, commits, pushes, branch/worktree operations, or device deployment.
