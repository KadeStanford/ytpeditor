# Automated test coverage

The test suite covers the editor at model, controller, media-engine, render, persistence, and real QML interaction levels. `ytp_controller_api_coverage` also fails whenever a new `Q_INVOKABLE` method is added to the Project, Timeline, or Export controller without a direct test reference.

| Area | Automated coverage |
| --- | --- |
| Core time/model | Rational time, frame-rate matrix, ranges, IDs, validation, 20k/100k-event scale |
| Project lifecycle | New/open/save, first import, automatic V1/A1 seeding, legacy full-source insertion, media activation/bins, missing-media relink, undo/redo |
| Recovery | Rotating autosaves, recovery, discard paths, edit journal paths, collected project ZIP |
| Clip library | In/Out creation, transcript clips, metadata, filters, sorting, favorites, thumbnails, deletion and undo |
| Timeline editing | Insert/overwrite/replace, linked selection, move, box/select-all, split, delete, duplicate, copy/paste, trim, slip, roll, fades, group/link operations, ripple and snapping |
| Tracks and controls | Add/remove tracks, lock/mute/solo/visibility/height/color, markers, zoom, visible range, configurable and VEGAS-default shortcuts |
| Inspector | Deterministic primary selection, transform/crop/flip, transform keys, linked speed/reverse, freeze, linked audio gain/pitch, captions and masks |
| Effects and audio | Clip/track/master effects, parameters, bypass/reset/reorder/remove, keyframes, presets, mixer gain/pan, envelopes and limiter |
| YTP toolkit | Stutter, rapid reverse, frame repeat, rhythm repeat, speed ladder, safe audio destruction, visual presets, Sentence Mixer, macros and seeded randomizer |
| Remix/finish | Local transcript paths, word/phonetic search, Sentence Mixer v2, beat detection/grid/tools, sequences, adjustment/nested clips, compounds, motion tracking and tracked motion |
| Preview/cache | Source multimedia decode/seek, timeline playhead bounds, Program frame refresh, rendered A/V preview, global preview offset, continuous cache, cancellation and cache clearing |
| Export | Presets/custom presets, validation, full-resolution source use, marked ranges, audio export, snapshots, queue/progress/history/logs, cancellation and codec failures |
| QML interaction | Every inspector tab/page instantiates without binding errors; real mouse ruler seeking, Program monitor switching, draggable playhead, and wheel scrolling on all six right-panel tabs |
| Packaging | Portable build stages dependencies and is smoke-tested through the packaged executable |
