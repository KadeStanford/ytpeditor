# Workspace launch rule

- Never launch `build\ucrt-debug\ytp_editor.exe` directly. It depends on the MSYS2 UCRT64 Qt/MLT runtime and will exit immediately when that runtime is absent from the caller's `PATH`.
- To launch an existing debug build, always run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\launch-dev.ps1` from the repository root.
- To rebuild and then launch, run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-dev.ps1`.
