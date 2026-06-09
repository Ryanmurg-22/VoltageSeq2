# Windows MSVC build — remaining fixes for CI

**Status:** macOS CI builds + releases automatically. The Windows CI job is wired
up (`.github/workflows/build.yml`) but the **Mutable Instruments code does not
yet compile cleanly under MSVC**. A previous local Windows build succeeded, so
these were solved once already — this note lists what remains so they can be
re-applied and **pushed** (the earlier fixes were never committed).

## For Claude on the Windows machine

1. `git pull origin main` first. The repo CMake now defines `TEST=1` and
   `_USE_MATH_DEFINES=1` and compiles the four `Plaits*.cpp` wrappers — that
   already fixed the macOS CMake build and the `M_PI` errors.
2. Build and fix whatever MSVC still rejects, then **commit and push** so CI
   picks it up. Verify locally with:
   ```bat
   cmake -B build -A x64
   cmake --build build --config Release --parallel
   ```

## Known remaining MSVC errors (from CI logs)

1. **`__attribute__((always_inline))` — `stmlib/utils/dsp.h` (~line 38+)**
   MSVC error C3646 / C2059. GCC/Clang attribute MSVC rejects.
   Fix: make the inline macro portable, e.g. in a small compat header force-
   included for MSVC, or guard:
   ```cpp
   #if defined(_MSC_VER)
     #define always_inline __forceinline
   #else
     #define always_inline inline __attribute__((always_inline))
   #endif
   ```
   (Match however stmlib declares it — neutralising `__attribute__` for MSVC,
   `#define __attribute__(x)`, is the blunt-but-effective alternative.)

2. **Zero-size array — `plaits/resources.cc` (~line 4032)**
   MSVC error C2466: "cannot allocate an array of constant size 0". GCC/Clang
   allow zero-length arrays as an extension; MSVC does not. Fix: give the array
   size 1, or guard the declaration for MSVC.

3. **(Should already be fixed by `_USE_MATH_DEFINES`)** `M_PI` in
   `plaits/dsp/oscillator/sine_oscillator.h` and the cascading
   "const object must be initialized" errors (`f_pi`, `opcodes_`, `renderers_`).
   Confirm these are gone after the pull.

Once it compiles, push — the next `v*` tag will auto-build the Windows
installer and attach it to the GitHub release alongside the macOS `.pkg`.
