# YTP Editor

A Windows-first, non-destructive video editor optimized for creating YouTube Poop edits. The product and implementation roadmap is in [PROJECT_PLAN.md](PROJECT_PLAN.md).

## Current state

All eight planned milestones are complete. The 0.5.0 alpha adds a full native UI overhaul: a coherent dark visual system, workspace presets, responsive panel prioritization, Source/Program/Dual monitors, a rebuilt timeline and clip presentation, YTP quick actions, modern navigation, polished onboarding, and automated multi-size visual regression checks. See [docs/MILESTONES.md](docs/MILESTONES.md) for verified results.

## Build on Windows

Prerequisites for the foundation build:

- Visual Studio 2022 with Desktop development with C++
- CMake 3.24 or newer

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Qt 6 and MLT are not required for the core tests. They are required to complete the playback/rendering spike and desktop UI.

The complete native build uses the `windows-ucrt-debug` preset:

```powershell
C:\msys64\ucrt64\bin\cmake.exe --preset windows-ucrt-debug
C:\msys64\ucrt64\bin\cmake.exe --build --preset windows-ucrt-debug
C:\msys64\ucrt64\bin\ctest.exe --preset windows-ucrt-debug
```

To build and launch the current editor:

```powershell
.\tools\run-dev.ps1
```

To launch an existing build without rebuilding, use either:

```powershell
.\tools\launch-dev.ps1
```

or double-click `launch-editor.cmd`. Do not invoke the debug EXE directly; the launcher supplies its Qt, MLT, and UCRT runtime environment.

To produce the release-mode portable ZIP and current-user setup EXE:

```powershell
.\tools\package-alpha.ps1
```
