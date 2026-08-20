# Third-party notices

The YTP Editor alpha bundles runtime components from Qt 6, FFmpeg, and the MSYS2 UCRT64 environment. Their source, license texts, and redistribution terms remain available from the upstream projects:

- Qt 6: https://www.qt.io/licensing/open-source-lgpl-obligations (LGPLv3/GPLv3 components)
- FFmpeg: https://ffmpeg.org/legal.html (LGPL/GPL depending on the packaged build configuration)
- MSYS2 packages and MinGW-w64 runtime: https://packages.msys2.org and https://www.mingw-w64.org

The current MSYS2 FFmpeg package is GPL-enabled. The alpha is therefore an evaluation build and must be redistributed in compliance with the GPL and all bundled component licenses. Before a public commercial release, freeze exact package versions, ship their complete license files and corresponding-source offer/source links, and repeat the dependency audit.

YTP Editor itself is currently an unreleased development project; no separate distribution license has yet been selected.
# Local transcription models

YTP Editor can invoke FFmpeg's optional Whisper filter with a model chosen by the user. No speech model is bundled. Users are responsible for the license and provenance of models they provide; model files are never copied into projects or release packages.
