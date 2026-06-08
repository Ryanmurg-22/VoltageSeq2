# Windows build handoff — for Claude Code on the Windows machine

You are picking up a task mid-stream. Read this fully before doing anything.

## Goal
Build the **Windows VST3** of VoltageSeq2, then produce a single-file
**Windows installer `.exe`** from it.

## Context
- This is a JUCE 8.0.12 audio plugin (VST3/AU instrument). JUCE is fetched
  automatically by CMake (`FetchContent` in `CMakeLists.txt`) — no manual JUCE
  setup needed, just internet access on the first configure.
- Current version: **v4.4**. Repo: https://github.com/Ryanmurg-22/VoltageSeq2
- The code has **only ever been compiled on macOS (Apple Silicon)**. This is the
  FIRST Windows compile, so expect possible platform issues (macOS-only headers,
  `std::` includes that MSVC needs explicitly, `#pragma` differences, etc.) in
  `Source/PluginProcessor.cpp` and `Source/PluginEditor.cpp`. Fix them as you go.
- Machine has **Visual Studio 2026** installed. `cmake` was NOT on the user's
  command-line PATH. Use one of:
  - VS 2026's bundled CMake (open the folder in VS), OR
  - the CMake that ships in VS at
    `...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`, OR
  - install standalone CMake and add to PATH.

## Step 1 — Configure
```bat
cmake -B build -A x64
```
(First run downloads JUCE — a few minutes.)

## Step 2 — Build the VST3 (Release)
```bat
cmake --build build --config Release --target VoltageSeq2_VST3
```
Output bundle: `build\VoltageSeq2_artefacts\Release\VST3\VoltageSeq2.vst3`

If the build fails, READ the compiler errors, fix the source, and rebuild. These
will almost certainly be small cross-platform fixes. After fixing, commit with a
clear message and push so the macOS maintainer can pull the fixes back.

## Step 3 — Build the installer .exe
Requires **Inno Setup 6** (https://jrsoftware.org/isdl.php).
The script is already in the repo: `installer\windows\VoltageSeq2.iss`.
```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\windows\VoltageSeq2.iss
```
Output: `installer\windows\Output\VoltageSeq2_v4.4_Windows_Installer.exe`

## Step 4 — Deliver
- Verify the installer runs and drops `VoltageSeq2.vst3` into
  `C:\Program Files\Common Files\VST3\`.
- The maintainer may want this `.exe` attached to the GitHub v4.4 release
  (https://github.com/Ryanmurg-22/VoltageSeq2/releases/tag/v4.4) via
  `gh release upload v4.4 <path-to-exe>` — confirm with the user first.

## Important
- Do NOT change DSP/audio logic to "fix" build errors — only make
  compatibility fixes (includes, platform guards). If a real logic change seems
  needed, stop and explain it to the user first.
- Commit any source fixes; push to `main`.
