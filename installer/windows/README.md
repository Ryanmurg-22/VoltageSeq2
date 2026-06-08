# Building VoltageSeq2 for Windows (VST3 + installer)

Produces `VoltageSeq2.vst3` and a single-file installer
`VoltageSeq2_v4.4_Windows_Installer.exe`.

## Prerequisites (install once)

1. **Visual Studio 2022** with the **"Desktop development with C++"** workload
   (gives you the MSVC compiler + the bundled CMake).
2. **Git** — https://git-scm.com/download/win
3. **Inno Setup 6** — https://jrsoftware.org/isdl.php (for the installer step).

## 1. Get the code

```bat
git clone https://github.com/Ryanmurg-22/VoltageSeq2.git
cd VoltageSeq2
```
(If you already have it: `git pull`.)

## 2. Configure with CMake

From the repo root, in a **Developer Command Prompt for VS 2022**:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
```

The first run downloads JUCE 8.0.12 automatically (needs internet) — give it a
few minutes.

## 3. Build the VST3 (Release)

```bat
cmake --build build --config Release --target VoltageSeq2_VST3
```

Output bundle:

```
build\VoltageSeq2_artefacts\Release\VST3\VoltageSeq2.vst3
```

> The build also tries to copy the plug-in into your shared VST3 folder
> automatically. If that copy step fails due to permissions, ignore it — the
> installer below handles deployment.

## 4. Build the installer .exe

1. Open `installer\windows\VoltageSeq2.iss` in **Inno Setup**.
2. Press **F9** (Build → Compile).
3. The installer appears at:
   ```
   installer\windows\Output\VoltageSeq2_v4.4_Windows_Installer.exe
   ```

That `.exe` installs `VoltageSeq2.vst3` into
`C:\Program Files\Common Files\VST3\` for any DAW to find.

### Command-line alternative (no GUI)

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\windows\VoltageSeq2.iss
```
