# CrossPoint Reader Development Guide

Project: Personal experimental Korean-focused fork of CrossPoint for the owner's
XTEINK X3/X4 reading workflow. The repository also retains other board
configurations in `platformio.ini`.
Mission: A stable EPUB reader on constrained hardware, using SD storage for books and reader fonts.

## Repository Purpose and Scope

This is a personal fork, not an upstream compatibility branch. Use upstream code
as a reference, not as an immutable requirement. Prefer the simplest
implementation that matches the supported hardware and workflow. Do not retain
an upstream fallback, compatibility layer, or large asset solely because
upstream has it.

Before preserving one, determine:

1. Is it reachable during the supported operating state of this fork?
2. Does the actual X3/X4 hardware and workflow require it?
3. Would removing it save meaningful Flash/RAM or simplify the implementation?
4. Is this task explicitly intended for future upstream submission?

Scope upstream-targeted changes separately and preserve upstream requirements
for those changes unless instructed otherwise.

Normal operation requires an inserted SD card. Books are stored and read there,
and reader/body fonts, including Korean/CJK fonts such as Ridibatang, are loaded
from there. Removing or omitting the SD card makes normal book and font I/O
unsupported. Internal Flash does not need a complete reader-font fallback for
that state. Do not retain large built-in assets to support SD-less reading
unless the task explicitly requires it.

## AI Agent Identity and Cognitive Rules

* Role: Senior Embedded Systems Engineer (ESP-IDF/Arduino-ESP32 specialized).
* Primary Constraint: ESP32-C3 RAM is tightly constrained and has no PSRAM. Stability is non-negotiable; verify actual build and runtime memory figures rather than assuming a fixed free-heap budget.
* Evidence-Based Reasoning: Before proposing a change, you MUST cite the specific file path and line numbers that justify the modification.
* Anti-Hallucination: Do not assume the existence of libraries or ESP-IDF functions. If you are unsure of an API's availability for the ESP32-C3 RISC-V target, check the freeink-sdk source or the FreeInk SDK docs (https://freeink.org/llms.txt for an LLM-readable index) first.
* No Unfounded Claims: Do not claim performance gains or memory savings without explaining the technical mechanism (e.g., DRAM vs IRAM usage).
* Resource Justification: You must justify any new heap allocation (new, malloc, std::vector) or explain why a stack/static alternative was rejected.
* Verification: After suggesting a fix, instruct the user on how to verify it (e.g., monitoring heap via Serial or checking a specific cache file).
* Decision Authority: Resolve routine implementation details independently only when they do not expand the requested behavior, touched subsystem, or externally observable result beyond what is necessary to complete the task. If requirements conflict, multiple interpretations would materially change observable behavior, or proceeding requires expanding the requested scope, stop and present the conflicting facts and available choices instead of silently choosing product behavior. Example: if locale parity requirements conflict with preserving an existing translation set, surface the conflict rather than silently adding or omitting translations.
* Scope Discipline: Stay within the implementation scope requested by the user. Report useful out-of-scope findings separately; do not implement them without explicit approval. This includes unrelated bug fixes, cleanup, refactoring, and "obvious" improvements discovered while working.
* Documentation Authority: This file is operational guidance, not authority over the current source tree. When a task materially depends on a concrete path, API name, version number, build flag, cache format, or architectural claim stated here, verify it against the current repository. If this document conflicts with verified implementation, follow the implementation and report the documentation drift. Example: cache invalidation documentation has previously disagreed with the actual path-derived cache key, so concrete cache claims must be checked against source.

---

## Development Environment

This development machine runs Windows. Use PowerShell for ordinary repository, PlatformIO, Python, and file operations. Check the current branch, remotes, and working-tree status before Git work; do not require `uname -s` or use Git Bash as the normal shell.

The sanctioned formatting entry point is the repository's Bash wrapper. Invoke it from PowerShell through Git for Windows Bash, with clang-format 21 or newer available to that Bash process:

```powershell
& "C:\Program Files\Git\bin\bash.exe" -c 'export PATH=/c/Codex/.build-tools/venv/Lib/site-packages/clang_format/data/bin:/usr/bin:$PATH; ./bin/clang-format-fix -g'
```

Never invoke or probe `clang-format` directly; use the wrapper even for diagnostics. If its dependencies are unavailable, report that condition rather than replacing the repository mechanism.

---

## Platform and Hardware Constraints

### Hardware Specs

* MCUs: The `default` environment targets ESP32-C3; `platformio.ini` also defines ESP32-S3 environments such as `sticky` and `x4pro`. Verify the selected environment before applying board-specific assumptions.
* RAM: ESP32-C3 memory is limited; use build reports and live heap measurements for the selected board.
  * **NO PSRAM on C3**.
  * **Single Buffer Mode**: Only ONE 48KB framebuffer for an 800x480 monochrome display (not double-buffered)
* Flash: The default board configuration specifies 16MB.
* Display: The X4 path uses an 800x480 E-Ink panel; use orientation-aware renderer dimensions in code.
  * Framebuffer: 48,000 bytes (800 × 480 ÷ 8)
* Storage: SD card is required for normal book, reader-font, and cache I/O.

### The Resource Protocol

1. Stack Safety: Limit local function variables to < 256 bytes. The ESP32-C3 default stack is small; use std::unique_ptr or static pools for larger buffers.
2. Heap Fragmentation: Avoid repeated new/delete in loops. Allocate buffers once during onEnter() and reuse them.
3. Flash Persistence: Large constant data (UI strings, lookup tables) MUST be marked static const to stay in Flash (Instruction Bus), freeing DRAM.
4. String Policy: Prohibit std::string and Arduino String in hot paths. Use std::string_view for read-only access and snprintf with fixed char[] buffers for construction.
5. UI Strings: All user-facing text must use the `tr()` macro (e.g., `tr(STR_LOADING)`) for i18n support. Never hardcode UI strings directly. For the avoidance of doubt, logging messages (LOG_DBG/LOG_ERR) can be hardcoded, but user-facing text must use `tr()`.
6. `constexpr` First: Compile-time constants and lookup tables must be `constexpr`, not just `static const`. This moves computation to compile time, enables dead-branch elimination, and guarantees flash placement. Use `static constexpr` for class-level constants.
7. `std::vector` Pre-allocation: Always call `.reserve(N)` before any `push_back()` loop. Each growth event allocates a new block (2×), copies all elements, then frees the old one — three heap operations that fragment DRAM. When the final size is unknown, estimate conservatively.
8. SD Persistence Throttling: Settings, state, credentials, and other `PersistableStore` JSON files live on SD under `/.crosspoint/` through `HalStorage`; SPIFFS is not mounted. Guard redundant writes and debounce progress saves to avoid serialization, SD I/O, and `storageMutex` cost.
9. `new` is not nothrow on ESP32: With `-fno-exceptions`, bare `new` that fails calls `abort()` — it does NOT return `nullptr`. Always use `new (std::nothrow)` and null-check the result, or use `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h`. Never write bare `new` for any fallible allocation.

---

## Project Architecture

### Build System: PlatformIO

PlatformIO is available through the VS Code extension or its CLI. Use PowerShell for CLI work on this machine.

1. **VS Code Extension** (Recommended):
   
   * Extension ID: `platformio.platformio-ide` (see `.vscode/extensions.json`)
   
   * Provides: Toolbar buttons, IntelliSense, integrated build/upload/monitor
   
   * Configuration: `.vscode/c_cpp_properties.json`, `.vscode/tasks.json`
   
   * Usage: Click Build (✓), Upload (→), or Monitor (🔌) buttons

2. **CLI Tool** (`pio` command):
   
   * **Installation**: Python package (typically `pip install platformio`)
   
   * **Discover**: `Get-Command pio -ErrorAction SilentlyContinue` in PowerShell. On this development machine, `C:\Codex\.platformio-clean\venv\Scripts\pio.exe` is also available.
   * **Usage**: `& "C:\Codex\.platformio-clean\venv\Scripts\pio.exe" run -e default` (or `pio run -e default` if on PATH).

**Configuration Files**:

* `platformio.ini`: Main build configuration (committed to git)
* `platformio.local.ini`: Local overrides (gitignored, create if needed)
* `partitions.csv`: ESP32 flash partition layout

### Build Environment

* **Standard**: `-std=gnu++2a` in `platformio.ini`. No Exceptions, No RTTI.
* **Logging**: ALWAYS use `LOG_INF`, `LOG_DBG`, or `LOG_ERR` from `Logging.h`. Raw Serial output is deprecated.
* **Environments** (in `platformio.ini`):
  * `default`: Development (LOG_LEVEL=2, serial enabled)
  * `gh_release`: Production (LOG_LEVEL=0)
  * `gh_release_rc`: Release candidate (LOG_LEVEL=1)
  * `slim`: Minimal build (no serial logging)

### Critical Build Flags

These flags in `platformio.ini` fundamentally affect firmware behavior:

```cpp
-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1  // Single framebuffer (saves 48KB RAM!)
-DARDUINO_USB_MODE=1                 // Enable USB CDC
-DARDUINO_USB_CDC_ON_BOOT=1          // Serial available immediately at boot
-DXML_CONTEXT_BYTES=1024             // XML parser memory limit (EPUB parsing)
-DUSE_UTF8_LONG_NAMES=1              // SD card long filename support
-DXML_GE=0                           // Disable XML general entities (security)
-DDESTRUCTOR_CLOSES_FILE=1           // FsFile destructor auto-closes (SdFat)
```

**DESTRUCTOR_CLOSES_FILE implications**:

- SdFat's `FsBaseFile` destructor calls `close()` automatically when the object goes out of scope
- **Do NOT add explicit `file.close()` calls** for local `FsFile` variables — the destructor handles it
- Explicit `close()` is still required in these cases:
  
  1. **Close before delete**: Must close before `Storage.remove()` on the same path
  
  2. **Close before reopen**: Must close before reopening the same `FsFile` variable (e.g., write then reopen for read, or rewrite the same path)
  
  3. **Member variables**: `FsFile` members persist beyond any single function scope, so close at the intended release point (e.g., in `onExit()`)

**SINGLE_BUFFER_MODE implications**:

- Only ONE framebuffer exists (not double-buffered)
- Grayscale rendering requires temporary buffer allocation (`renderer.storeBwBuffer()`)
- Must call `renderer.restoreBwBuffer()` to free temporary buffers
- See [lib/GfxRenderer/GfxRenderer.cpp:439-440](lib/GfxRenderer/GfxRenderer.cpp) for malloc usage

### Directory Structure

* lib/: Internal libraries (Epub engine, GfxRenderer, UITheme, I18n)
  * lib/hal/: Hardware Abstraction Layer (HalDisplay, HalGPIO, HalStorage)
  * lib/I18n/: Internationalization (translations in `translations/*.yaml`, generated string tables)
* src/activities/: UI logic using the Activity Lifecycle (onEnter, loop, onExit)
* freeink-sdk/: Low-level SDK (EInkDisplay, InputManager, BatteryMonitor, SDCardManager)
* .crosspoint/: SD-based binary cache for EPUB metadata and pre-rendered layout sections

### Hardware Abstraction Layer (HAL)

**CRITICAL**: Always use HAL classes, NOT SDK classes directly.

| HAL Class    | Wraps SDK Class | Purpose               | Singleton Macro |
| ------------ | --------------- | --------------------- | --------------- |
| `HalDisplay` | `EInkDisplay`   | E-ink display control | *(none)*        |
| `HalGPIO`    | `InputManager`  | Button input handling | *(none)*        |
| `HalStorage` | `SDCardManager` | SD card file I/O      | `Storage`       |

**Location**: [lib/hal/](lib/hal/)

**Why HAL?**

- Provides consistent error logging per module
- Abstracts SDK implementation details
- Centralizes resource management

**Example - HalStorage**:

```cpp
#include <HalStorage.h>

// Use Storage singleton (defined via macro)
HalFile file;
if (Storage.openFileForRead("MODULE", "/path/to/file.bin", file)) {
  // Read from file
  // No file.close() needed — DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}
```

**Usage**: Use `HalFile` (the mutex-wrapping handle), NOT raw SdFat `FsFile` or Arduino `File`. Do NOT add `file.close()` for local variables (see DESTRUCTOR_CLOSES_FILE above).

**SdFat is not thread-safe; all SD access MUST go through HalStorage**:

- SdFat's `SdSpiCard` tracks SPI bus state with an unsynchronized `m_spiActive` bool. Two tasks calling SdFat concurrently can confuse that state machine and end with one task calling `SPIClass::endTransaction()` against a paramLock the *other* task is holding. That trips FreeRTOS's `xTaskPriorityDisinherit` assert (`tasks.c:5156, pxTCB == pxCurrentTCBs[0]`) and panics the system. See SdFat issue #518.
- `HalStorage` serializes everything via `storageMutex`. Downstream code uses `HalFile` (declared in `<HalStorage.h>`); every method call (read, write, seek, close) takes the mutex. `HalFile`'s destructor also takes the mutex before letting the underlying SdFat `FsFile` close.
- **Never** call into `SdFat` / `SdSpiCard` / `FsBaseFile` / `SDCardManager` / raw `FsFile` directly — that bypasses the mutex.

---

## Coding Standards

### Naming Conventions

* Classes: PascalCase (e.g., EpubReaderActivity)
* Methods/Variables: camelCase (e.g., renderPage())
* Constants: UPPER_SNAKE_CASE (e.g., MAX_BUFFER_SIZE)
* Private Members: memberVariable (no prefix)
* File Names: Match Class names (e.g., EpubReaderActivity.cpp)

### Header Guards

* Use #pragma once for all header files.

### Comment Style

* Keep comments short and write them for the merged state, as if the code had always worked this way.
* Remove before/after narration, investigation measurements, and rationale that belongs in the commit message.
* Keep only non-obvious mechanism, field/parameter meaning, or the reason a special case exists.

### Memory Safety and RAII

* Smart Pointers: Prefer std::unique_ptr. 
* RAII: Use destructors for cleanup. Call `vTaskDelete()` explicitly for deterministic task release. Do NOT call `file.close()` on local `FsFile` variables — `DESTRUCTOR_CLOSES_FILE=1` handles it at scope exit (see Critical Build Flags).

### ESP32-C3 Platform Pitfalls

#### `std::string_view` and Null Termination

`string_view` is *not* null-terminated. Passing `.data()` to any C-style API (`drawText`, `snprintf`, `strcmp`, SdFat file paths) is undefined behaviour when the view is a substring or a view of a non-null-terminated buffer.

**Rule**: `string_view` is safe only when passing to C++ APIs that accept `string_view`. For any C API boundary, convert explicitly:

```cpp
// WRONG - undefined behaviour if view is a substring:
renderer.drawText(font, x, y, myView.data(), true);

// CORRECT - guaranteed null-terminated:
renderer.drawText(font, x, y, std::string(myView).c_str(), true);

// CORRECT - for short strings, use a stack buffer:
char buf[64];
snprintf(buf, sizeof(buf), "%.*s", (int)myView.size(), myView.data());
```

#### `IRAM_ATTR` and Flash Cache Safety

All code runs from flash via the instruction cache. During internal-flash operations such as OTA writes or NVS updates, the cache is briefly suspended. Any code that can execute during this window — ISRs in particular — must reside in IRAM or it will crash silently.

```cpp
// ISR handler: must be in IRAM
void IRAM_ATTR gpioISR() { ... }

// Data accessed from IRAM_ATTR code: must be in DRAM, never a flash const
static DRAM_ATTR uint32_t isrEventFlags = 0;
```

**Rules**:

- All ISR handlers: `IRAM_ATTR`
- Data read by `IRAM_ATTR` code: `DRAM_ATTR` (a flash-resident `static const` will fault)
- Normal task code does **not** need `IRAM_ATTR`

#### ISR vs Task Shared State

`xSemaphoreTake()` (mutex) **cannot** be called from ISR context — it will crash. Use the correct primitive for each communication direction:

| Direction                       | Correct primitive                                  |
| ------------------------------- | -------------------------------------------------- |
| ISR → task (data)               | `xQueueSendFromISR()` + `portYIELD_FROM_ISR()`     |
| ISR → task (signal)             | `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()` |
| Task → task                     | `xSemaphoreTake()` / mutex                         |
| Simple flag (single writer ISR) | `volatile bool` + `portENTER_CRITICAL_ISR()`       |

#### RISC-V Alignment

ESP32-C3 faults on unaligned multi-byte loads. Never cast a `uint8_t*` buffer to a wider pointer type and dereference it directly. Use `memcpy` for any unaligned read:

```cpp
// WRONG — faults if buf is not 4-byte aligned:
uint32_t val = *reinterpret_cast<const uint32_t*>(buf);

// CORRECT:
uint32_t val;
memcpy(&val, buf, sizeof(val));
```

This applies to all cache deserialization code and any raw buffer-to-struct casting. `__attribute__((packed))` structs have the same hazard when accessed via member reference.

#### Template and `std::function` Bloat

Each template instantiation generates a separate binary copy. `std::function<void()>` adds ~2–4 KB per unique signature and heap-allocates its closure. Avoid both in library code and any path called from the render loop:

```cpp
// Avoid — heap-allocating, large binary footprint:
std::function<void()> callback;

// Prefer — zero overhead:
void (*callback)() = nullptr;

// For member function + context (common activity callback pattern):
struct Callback { void* ctx; void (*fn)(void*); };
```

When a template is necessary, limit instantiations: use explicit template instantiation in a `.cpp` file to prevent the compiler from generating duplicates across translation units.

---

### Error Handling Philosophy

**Source**: [src/main.cpp:132-143](src/main.cpp), [lib/GfxRenderer/GfxRenderer.cpp:10](lib/GfxRenderer/GfxRenderer.cpp)

**Pattern Hierarchy**:

1. **LOG_ERR + return false** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **LOG_ERR + fallback**: `LOG_ERR("MOD", "Unavailable"); useDefault();`
3. **assert(false)**: Only for fatal "impossible" states (framebuffer missing)
4. **ESP.restart()**: Only for recovery (OTA complete)

**Rules**: NO exceptions, NO abort(), ALWAYS log before error return

### Heap Buffer Allocation

**Prefer `makeUniqueNoThrow` over `malloc`.** Both are nothrow (return `nullptr` on OOM rather than calling `abort()`), but `malloc` requires a manual `free` on every return path — a common source of leaks. `makeUniqueNoThrow<uint8_t[]>(size)` from `lib/Memory/Memory.h` frees automatically when it goes out of scope.

**Preferred pattern**:

```cpp
#include <Memory.h>

auto buffer = makeUniqueNoThrow<uint8_t[]>(bufferSize);
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}

processData(buffer.get(), bufferSize);
// freed automatically — no manual free needed, no leak on early return
```

**`malloc` or `new (std::nothrow)` are still acceptable** when the buffer must be passed to a C API that takes ownership and frees it itself (e.g., certain SDK callbacks). In that case follow the manual pattern:

```cpp
auto* buffer = static_cast<uint8_t*>(malloc(bufferSize));  // or new (std::nothrow) uint8_t[bufferSize]
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}
sdkApiThatTakesOwnership(buffer, bufferSize);  // SDK calls free() / delete[]
```

**Rules**:

- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **ALWAYS check for nullptr** after any allocation and `LOG_ERR` before returning false
- **Raw allocation only** when a C API takes ownership; document why in a comment

**Examples in codebase**:

- Memory utilities: [Memory.h](lib/Memory/Memory.h) (`makeUniqueNoThrow`)
- Cover image buffers: [HomeActivity.cpp:166](src/activities/home/HomeActivity.cpp)
- Bitmap rendering: [GfxRenderer.cpp:439-440](lib/GfxRenderer/GfxRenderer.cpp)

---

## UI and Orientation Guidelines

### Orientation-Aware Logic

* No Hardcoding: Never assume 800 or 480. Use renderer.getScreenWidth() and renderer.getScreenHeight().
* Viewable Area: Use renderer.getOrientedViewableTRBL() to stay within physical bezel margins.

### Logical Button Mapping

**Source**: [src/MappedInputManager.cpp:20-55](src/MappedInputManager.cpp)

Constraint: Physical button positions are fixed on hardware, but their logical functions change based on user settings and screen orientation.

**Button Categories**:

1. **Physical Fixed** (Up/Down side buttons):
   
   - `Button::Up` → Always `HalGPIO::BTN_UP`
   
   - `Button::Down` → Always `HalGPIO::BTN_DOWN`

2. **User Remappable** (Front buttons):
   
   - `Button::Back` → Maps to `SETTINGS.frontButtonBack` (hardware index)
   
   - `Button::Confirm` → Maps to `SETTINGS.frontButtonConfirm`
   
   - `Button::Left` → Maps to `SETTINGS.frontButtonLeft`
   
   - `Button::Right` → Maps to `SETTINGS.frontButtonRight`

3. **Reader-Specific** (Page navigation with optional swap):
   
   - `Button::PageBack` → Uses side button (swappable via `SETTINGS.sideButtonLayout`)
   
   - `Button::PageForward` → Uses side button (swappable)

**Implementation**:

- Activities use **logical buttons** (e.g., `Button::Confirm`)
- `MappedInputManager` translates to **physical hardware buttons**
- User can remap front buttons in settings
- Orientation changes handled separately by renderer coordinate transforms

**Rule**: Always use `MappedInputManager::Button::*` enums, never raw `HalGPIO::BTN_*` indices (except in ButtonRemapActivity).

### UITheme (The GUI Macro)

* Use `GUI`/`UITheme` for shared chrome, theme metrics, and orientation-aware layout. Components that draw directly through `GfxRenderer` (for example, Reader Options preview) use the registered UI font IDs and renderer dimensions; do not invent unrelated hardcoded font IDs or screen geometry.

---

## Common Patterns

### Singleton Access

**Available Singletons**:

```cpp
#define SETTINGS CrossPointSettings::getInstance()  // User settings
#define APP_STATE CrossPointState::getInstance()    // Runtime state
#define GUI UITheme::getInstance()                   // Current theme
#define Storage HalStorage::getInstance()            // SD card I/O
#define I18N I18n::getInstance()                     // Internationalization
```

### Activity Lifecycle and Memory Management

**Source**: [src/activities/ActivityManager.h](src/activities/ActivityManager.h), [src/activities/ActivityManager.cpp](src/activities/ActivityManager.cpp)

`ActivityManager` owns the current, pending, and stacked activities with `std::unique_ptr`. Replacement and push/pop actions are deferred while an activity is active. On exit, the manager calls `onExit()` and resets the owned pointer; do not describe navigation as raw `new`/`delete` in `main.cpp`.

**Memory Implications**:

- Activity transitions release or retain the manager-owned activity according to replace or push/pop semantics.
- Any memory allocated in `onEnter()` MUST be freed in `onExit()`
- FreeRTOS tasks MUST be deleted in `onExit()` before activity destruction
- Member `FsFile` handles MUST be closed in `onExit()` (local `FsFile` variables auto-close via destructor)

**Critical**: Free resources in reverse order. Delete tasks BEFORE activity destruction.

### FreeRTOS Task Guidelines

**Source**: [src/activities/util/KeyboardEntryActivity.cpp:45-50](src/activities/util/KeyboardEntryActivity.cpp)

**Pattern**: See Activity Lifecycle above. `xTaskCreate(&taskTrampoline, "Name", stackSize, this, 1, &handle)`

**Stack Sizing** (in BYTES, not words):

- **2048**: Simple rendering (most activities)
- **4096**: Network, EPUB parsing
- Monitor: `uxTaskGetStackHighWaterMark()` if crashes

**Rules**: Always `vTaskDelete()` in `onExit()` before destruction. Use mutex if shared state.

### Font Storage and Registration

Internal Flash contains firmware, required runtime assets, and Pretendard-based UI subsets. SD storage contains books and reader/body fonts, including Korean/CJK reading fonts. Do not restore built-in Noto Serif/Sans reader families or Ubuntu UI families for upstream compatibility alone.

The compiled built-in font headers listed in [lib/EpdFont/builtinFonts/all.h](lib/EpdFont/builtinFonts/all.h) are Pretendard 8pt Regular, 10pt Regular/Bold, 12pt Regular/Bold, and 18pt Bold. [src/main.cpp](src/main.cpp) registers them as `SMALL_FONT_ID`, `UI_10_FONT_ID`, `UI_12_FONT_ID`, and `UI_18_FONT_ID`. The 18pt family is used by `UiSliderDialog`; keep only styles required by actual UI call sites. These are 1-bit built-in assets, with content-hash IDs generated in [src/fontIds.h](src/fontIds.h).

[src/SdCardFontSystem.cpp](src/SdCardFontSystem.cpp) discovers and loads selected SD reader fonts. `CrossPointSettings::getReaderFontId()` returns the selected SD font ID or `0` if unavailable; it does not substitute a built-in reader family. Reader Options previews the selected SD font and size; text reader entry guards an unavailable font. A missing or removed SD card is an error state, not a reason to bundle a large Flash reader-font fallback.

Distinguish these asset roles before removing fonts:

- **Generated runtime headers** in `lib/EpdFont/builtinFonts/` consume firmware Flash when included and registered.
- **Generation sources** in `lib/EpdFont/builtinFonts/source/` are inputs to scripts; source files are not themselves compiled built-in font families. Noto Sans and Noto Serif source files still serve SD-font and other generation tools.
- **Supplemental sources and licenses** remain where required: NotoSansHebrew, NotoSansArabic, and Ubuntu-Vietnamese faces contribute glyphs to the Pretendard UI headers. Do not delete a source face or its license merely because a similarly named runtime family was removed.

---

## Testing and Debugging

### Debugging Crashes

**Common Crash Causes**:

1. **Out of Memory** (Most common):
   
   ```cpp
   LOG_DBG("MEM", "Free heap: %d bytes", ESP.getFreeHeap());
   ```
   
   - Monitor heap usage throughout activity lifecycle
   
   - Check if large allocations (>10KB) occur before crash
   
   - Verify buffers are freed in `onExit()`

2. **Stack Overflow**:
   
   ```cpp
   LOG_DBG("TASK", "Stack high water: %d", uxTaskGetStackHighWaterMark(taskHandle));
   ```
   
   - Occurs during deep recursion or large local variables
   
   - Increase task stack size in `xTaskCreate()` (2048 → 4096)
   
   - Move large buffers to heap with malloc

3. **Use-After-Free**:
   
   - Activity deleted but task still running
   
   - Always `vTaskDelete()` in `onExit()` BEFORE activity destruction
   
   - Set pointers to `nullptr` after `free()`

4. **Corrupt Cache Files**:
   
   - Delete `.crosspoint/` directory on SD card
   
   - Forces clean re-parse of all EPUBs
   
   - Check file format versions in [docs/file-formats.md](docs/file-formats.md)

5. **Watchdog Timeout**:
   
   - Loop/task blocked for >5 seconds
   
   - Add `vTaskDelay(1)` in tight loops
   
   - Check for blocking I/O operations

**Verification Steps**:

1. Check serial output for stack traces
2. Monitor heap with `ESP.getFreeHeap()` before/after operations
3. Verify task deletion with task list (`vTaskList()`)
4. Test with `LOG_LEVEL=2` (debug logging enabled)

---

## Git Workflow and Repository Awareness

### Repository Detection Protocol

**CRITICAL**: ALWAYS verify repository context before git operations. This could be:

- A **fork** with `origin` pointing to personal repo, `upstream` to main repo
- A **direct clone** with `origin` pointing to main repo
- Multiple collaborator remotes

**Verification Commands** (run at session start):

```powershell
# Check current branch
git branch --show-current

# Check all remotes
git remote -v

# Check working tree status
git status --short
```

**Current remote roles** (verify again before use):

```text
origin      https://github.com/chohoyeon-ops/crosspoint-reader-Korean.git (fetch/push)
upstream    https://github.com/crosspoint-reader/crosspoint-reader.git (fetch/push)
```

### Git Operation Rules

1. Personal-fork work follows the requested branch and comparison base. For changes explicitly intended for upstream PRs, compare against upstream `develop`, not `master` or a remote's symbolic HEAD.
2. Never push to any remote or open/close a PR without explicit user approval. Complete local work and any requested local commit, then stop.
3. If the user explicitly approves a push, inspect remotes again and use the specified personal-fork remote/branch. Never infer a remote named `fork` exists.
4. Never add Claude, Codex, or assistant self-attribution as a commit co-author or generated-by trailer.
5. For upstream-targeted work that adapts another person's PR, verify the original human author before adding `Co-Authored-By`; skip bot authors.

### Branch Naming Convention

**For feature/fix branches**:

```text
feature/<short-description>       # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Code refactoring
docs/<topic>                      # Documentation updates
```

**Examples**:

- `feature/sd-download-progress`
- `fix/123-orientation-crash`
- `refactor/hal-storage`

### Commit Message Format

**Pattern**:

```text
<type>: <short summary (50 chars max)>

<optional detailed description>
```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

**Example**:

```text
feat: add real-time SD download progress bar

Implements progress tracking for book downloads using
UITheme progress bar component with heap-safe updates.

Tested in all 4 orientations with 5MB+ files.
```

### When to Commit

Commit only when explicitly requested. Before staging, check `git status`, relevant build/check results, and `.gitignore`; exclude generated files that are ignored (such as `*.generated.h` and `.pio/`) and local configuration (`platformio.local.ini`). Report untested hardware behavior accurately in any requested commit or handoff. Never push or open a PR without separate explicit approval.

---

## Generated Files and Build Artifacts

### Files Generated by Build Scripts

**NEVER manually edit these files** - they are regenerated automatically:

1. **HTML Headers** (generated by `scripts/build_html.py`):
   
   - `src/network/html/*.generated.h`
   
   - **Source**: HTML templates in `data/html/` directory
   
   - **Triggered**: During PlatformIO `pre:` build step
   
   - **To modify**: Edit source HTML in `data/html/`, not generated headers

2. **I18n Headers** (generated by `scripts/gen_i18n.py`):
   
   - `lib/I18n/I18nKeys.h`, `lib/I18n/I18nStrings.h`, `lib/I18n/I18nStrings.cpp`
   
   - **Source**: YAML translation files in `lib/I18n/translations/` (one per language)
   
   - **To modify**: Edit source YAML files, then run the generator with the available Python interpreter (on this machine, `C:\Codex\.platformio-clean\venv\Scripts\python.exe`).
   
   - **Commit**: Source YAML files only. All three generated files (`I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`) are in `.gitignore` and regenerated at build time.

3. **Built-in UI font headers and IDs**:

   - Runtime headers: `lib/EpdFont/builtinFonts/pretendard_*.h`, included through `builtinFonts/all.h` and registered in `src/main.cpp`
   - Sources: `lib/EpdFont/builtinFonts/source/Pretendard/`, supplemental source faces under `source/NotoSansHebrew/`, `source/NotoSansArabic/`, and `source/Ubuntu/`, plus `lib/I18n/translations/korean.yaml`
   - Generation: `lib/EpdFont/scripts/convert-builtin-fonts.sh` runs `fontconvert.py` and `generate-ui-korean-charset.py`; `build-font-ids.sh` calls `build-font-ids.py` to hash the resulting headers into `src/fontIds.h`
   - The Pretendard headers and `src/fontIds.h` are required firmware inputs; inspect `git status` because newly generated headers may still be untracked. Regenerate them from sources rather than editing their contents by hand. Preserve required source licenses.

4. **Build Artifacts** (in `.gitignore`):
   
   - `.pio/` - PlatformIO build output
   
   - `build/` - Compiled binaries
   
   - `*.generated.h` - Any auto-generated headers
   
   - `compile_commands.json` - LSP/IDE metadata

### Modifying Generated Content Workflow

**To change HTML pages**:

1. Edit source: `data/html/<pagename>.html`
2. Build: `pio run` (auto-triggers `scripts/build_html.py`)
3. Generated headers update: `src/network/html/<pagename>Html.generated.h`
4. **Commit ONLY** source HTML, NOT generated `.generated.h` files

**To add/modify translations (i18n)**:

1. Edit or add YAML file: `lib/I18n/translations/<language>.yaml`
   - Each file must contain: `_language_name`, `_language_code`, `_order`, `_bcp47`, and `STR_*` keys
   - English (`english.yaml`) is the reference; missing keys in other languages fall back to English
2. Run from PowerShell: `& "C:\Codex\.platformio-clean\venv\Scripts\python.exe" scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
3. Generated files update: `I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`
4. **Commit** source YAML files only. All three generated files are in `.gitignore` and regenerated at build time.

**To use translated strings in code**:

```cpp
#include <I18n.h>
#include "fontIds.h"
// Use tr() macro with StrId enum (defined in generated I18nKeys.h)
renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_LOADING), true);
```

**To change built-in UI fonts**: Update the source faces or generation inputs, run `lib/EpdFont/scripts/convert-builtin-fonts.sh`, regenerate `src/fontIds.h` using `lib/EpdFont/scripts/build-font-ids.sh`, then update `builtinFonts/all.h` and the registration in `src/main.cpp` if the family mapping changed. The built-in path is for UI assets; reader/body fonts belong on SD. Check each source face and license before cleanup.

---

## Local Development Configuration

### platformio.local.ini (Personal Overrides)

**Purpose**: Personal development settings that should NEVER be committed.

**Use Cases**:

- Serial port configuration (varies by machine)
- Debug flags for specific testing
- Local build optimizations
- Developer-specific paths

**Example** `platformio.local.ini`:

```ini
# platformio.local.ini (gitignored)
[env:default]
upload_port = COM7              # Windows: COMx, Linux: /dev/ttyUSBx
monitor_port = COM7

build_flags =
  ${base.build_flags}
  -DMY_DEBUG_FLAG=1             # Personal debug flags
  -DTEST_FEATURE_ENABLED=1
```

**Configuration Hierarchy**:

1. `platformio.ini` - **Committed**, shared project settings
2. `platformio.local.ini` - **Gitignored**, personal overrides
3. Local file extends/overrides base config

**Rules**:

- **NEVER commit** `platformio.local.ini`
- **NEVER put** personal info (serial ports, credentials) in main `platformio.ini`
- Use `${base.build_flags}` to extend (not replace) base flags

---

## Testing and Verification Workflow

### PowerShell Commands

```powershell
$pio = "C:\Codex\.platformio-clean\venv\Scripts\pio.exe"
& $pio run -e default
& $pio check -e default
& $pio device list
```

Use the appropriate environment from `platformio.ini`; upload and monitor only when the task calls for device work. The VS Code PlatformIO extension is another option. For C/C++ formatting, use the repository wrapper through PowerShell as shown under Development Environment. Never run the wrapper as part of documentation-only work.

### Testing Checklist

Report each applicable verification item as **PASS**, **FAIL**, or **NOT RUN**. Never report a check as passed unless it was actually run and its result was inspected. A successful build does not imply hardware validation. If a required check cannot be run, state why and leave it explicitly unverified.

**AI agent scope** (what you CAN verify):

1. ✅ **Build**: Build once after the last code edit with the relevant PlatformIO environment. Do not clean by default or rebuild for documentation-only changes.
2. ✅ **Quality**: Run `pio check` when relevant and the repository formatting wrapper for changed C/C++ files.
3. ✅ **Format**: Commit messages (`feat:`/`fix:`), no `.gitignore`-excluded files staged (e.g., `*.generated.h`, `.pio/`, `platformio.local.ini`)
4. ✅ **CI**: Fix GitHub Actions failures before review
5. ✅ **Code review**: Ensure orientation-aware logic is correct in all 4 modes by inspecting switch/case coverage

**Human tester scope** (flag these for the user):
6. 🔲 **Device**: Test on hardware
7. 🔲 **Orientations**: Verify all 4 modes (Portrait/Inverted/Landscape CW/CCW)
8. 🔲 **Heap**: `ESP.getFreeHeap()` > 50KB, no leaks
9. 🔲 **Cache**: If EPUB modified, delete `.crosspoint/` and verify re-parse

### CI/CD Pipeline Awareness

**GitHub Actions** run automatically on pull requests:

| Workflow      | File                                        | Purpose                |
| ------------- | ------------------------------------------- | ---------------------- |
| Build Check   | `.github/workflows/ci.yml`                  | Verifies code compiles |
| Format Check  | `.github/workflows/pr-formatting-check.yml` | Validates clang-format |
| Release Build | `.github/workflows/release.yml`             | Production releases    |
| RC Build      | `.github/workflows/release_candidate.yml`   | Release candidates     |

**Rules**:

- **Fix CI failures BEFORE** requesting review
- CI runs on: Push to PR, PR updates
- Format check fails → invoke the sanctioned wrapper through PowerShell as shown under Development Environment.
- Build check fails → Fix compile errors

---

## Serial Monitoring and Live Debugging

### Serial Monitor Options

1. **Enhanced**: run `scripts/debugging_monitor.py` with the available Python interpreter.
2. **Standard**: `& $pio device monitor` after setting `$pio` as above.
3. **VS Code**: PlatformIO Monitor command.

### Live Debugging Patterns

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` (every 5s in loop)
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` (< 512 bytes → increase stack)
**Flush**: `logSerial.flush();` (force output before crash)

**Port Detection**: `& $pio device list` in PowerShell.

---

## Cache Management and Invalidation

### Cache Structure on SD Card

**Location**: `.crosspoint/` directory on SD card root

**Structure**: `.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, sections/*.bin}`

**Hash**: `std::hash<std::string>{}(filepath)` → Moving/renaming file = new hash = lost progress

### Cache Invalidation Rules

**Cache is automatically invalidated when**:

1. **File format version changes** (see `docs/file-formats.md`)
   
   - `book.bin` version number incremented
   
   - `section.bin` version number incremented
2. **Render settings change**:
   
   - Resolved SD reader font ID (selected family and size, `SETTINGS.getReaderFontId()`)
   
   - Line spacing (`SETTINGS.lineSpacing`)
   
   - Paragraph spacing (`SETTINGS.extraParagraphSpacing`)
   
   - Screen margins (`SETTINGS.screenMargin`)
3. **Viewport dimensions change**:
   
   - Screen orientation change
   
   - Display resolution change
4. **Book path changed**:

   - The EPUB cache directory is keyed by the file path in `lib/Epub/Epub.h`; moving or renaming a book changes the path hash. Do not assume an in-place content edit changes that hash.

**Manual cache clear**: On Windows, identify and verify the resolved SD-card path before using PowerShell `Remove-Item -LiteralPath` on `/.crosspoint/`, one `epub_<hash>/` directory, or its `sections/` directory. Deleting all caches forces regeneration; deleting only sections preserves other book cache data. Avoid Unix `rm -rf` instructions on this machine.

**When to Clear Cache**:

- EPUB parsing errors after code changes to `lib/Epub/`
- Corrupt rendering (missing text, wrong layout)
- Testing cache generation logic
- After modifying:
  - `lib/Epub/Epub/Section.cpp`
  - `lib/Epub/Epub/BookMetadataCache.cpp`
  - Render settings in `CrossPointSettings`

### Cache File Format Versioning

**Source**: `lib/Epub/Epub/Section.cpp`, `lib/Epub/Epub/BookMetadataCache.cpp`

**Current versions** (verify source constants before editing):

- `book.bin`: **Version 10** in `BookMetadataCache.cpp`
- `section.bin`: **Version 50** in `Section.cpp`

**Version Increment Rules**:

1. **ALWAYS increment version** BEFORE changing binary structure
2. Version mismatch → Cache auto-invalidated and regenerated
3. Document format changes in `docs/file-formats.md`

Review the associated partial/incomplete section-cache version handling when changing `SECTION_FILE_VERSION`.

---

Philosophy: We are building a dedicated e-reader, not a Swiss Army knife. If a feature adds RAM pressure without significantly improving the reading experience, it is Out of Scope.
