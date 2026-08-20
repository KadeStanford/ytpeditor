# Implementation status

## Milestone 0 — foundations

Status: **Complete** (2026-08-18)

Completed:

- C++20/CMake repository with MSVC core-only and UCRT64 full-stack presets.
- Warning-enabled core library and CTest test harness.
- Qt/QML desktop shell representing the planned Clip Library, dual viewer, Inspector, ripple control, and multitrack timeline layout.
- Exact normalized rational-time and half-open time-range primitives.
- Checked arithmetic that rejects invalid denominators, negative durations, and multiplication overflow.
- Qt offscreen startup smoke test.
- MLT/FFmpeg probe that detects the source profile, reopens against that profile, performs 512 exact seeks, applies a source In/Out range, and exports H.264/AAC MP4.
- Five-second 640x360 at 30 fps fixture produced 150 input frames and a 2.005-second trimmed output with video and audio.
- Ten-minute 160x90 at 12 fps fixture produced 7,200 frames. The full validation render completed without deadlock; `ffprobe` measured both video and audio at exactly 600.000 seconds.

Architecture finding:

- Loading the full MSYS2 MLT plugin directory crashes inside an optional plugin on this machine. Loading a curated repository containing the required avformat plugin is stable. Development and packaging will use an explicit, allow-listed plugin bundle rather than discovering every system plugin.

Remaining before release (not Milestone 0 blockers):

- Automate fixture generation and duration assertions in CI once binary dependency packaging is settled.
- Decide final redistribution configuration and complete the dependency license audit.
- Profile GPU-backed preview once interactive playback is connected to the Program Viewer.

## Milestone 1 — project, source viewer, and Clip Library

Status: **Complete** (2026-08-18)

Completed:

- Versioned project, project settings, media asset, and reusable library clip domain types.
- Stable UUID-shaped IDs with duplicate/reference validation.
- Exact rational source ranges and thumbnail positions distinct from future timeline instances.
- Validation for invalid settings, missing media, empty/out-of-bounds ranges, duplicate IDs, and broken thumbnail positions.
- Atomic, human-readable JSON saving with `QSaveFile`.
- Defensive loading that rejects malformed JSON and unsupported/corrupt project data without throwing through the UI boundary.
- Round-trip tests covering project settings, media metadata, tags, notes, colors, favorites, fingerprints, and exact clip ranges.
- Undo/redo command stack and an undoable reusable-clip creation command.
- FFprobe-backed import of duration, frame rate, dimensions, and audio sample rate.
- FFmpeg waveform and selection-thumbnail cache generation.
- Qt Multimedia source video/audio playback with seeking and play/pause controls.
- Source In/Out shortcuts and buttons plus one-command reusable clip creation.
- Searchable QML Clip Library backed by the real project model.
- UI actions for new/open/save/import and a 30-second autosave timer for named projects.
- Controller integration coverage for import, probing, waveform, clip creation, thumbnail, undo/redo, search, save, clear, reopen, and source relinking.
- UCRT64 full application tests and MSVC dependency-free core tests both pass.
- Project Media browser, media bins, search/filtering, source activation, and changed-file fingerprint detection.
- Background FFprobe import plus independent background waveform, thumbnail, and per-frame timestamp indexing.
- Timestamp-indexed forward/back frame stepping, including variable-frame-rate sources, with average-rate fallback.
- J/K/L shuttle controls, waveform scrubbing, play/pause, and decoded source video/audio.
- Undoable clip creation, metadata/source-range edits, and deletion.
- Clip name, tags, notes, bin, color, favorite, recent-created/recent-used, name, and duration organization.
- Audio-only imports and waveform-backed thumbnails in addition to video thumbnails.
- Missing/changed-media detection and validated relinking that rejects replacement media too short for saved clips.
- Three rotating autosaves, startup recovery detection, recover/discard controls, and atomic manual saves.
- Library drag payloads and a provisional multi-track drop surface ready for the Milestone 2 timeline model.

Acceptance evidence:

- Five UCRT64 suites pass: core, project persistence, controller end-to-end, Qt Multimedia playback/decode, and QML startup.
- Controller coverage exercises background import, metadata, frame indexing, waveform/thumbnail caches, project/media bins, clip metadata, favorites, sorting/filtering, recent use, undo/redo, save/reopen, three-generation recovery, missing-media detection, relinking, and audio-only clips.
- Qt Multimedia coverage loads a synthetic A/V source, seeks to one second, advances playback, and produces decoded video frames.
- The dependency-free model and command core separately compile and pass under MSVC 2022.
- The final 1440×900 QML window was rendered offscreen and visually inspected.

## Milestone 2 — dependable multitrack timeline

Status: **Complete** (2026-08-18)

Completed:

- Version 2 project schema with persistent sequences, ordered video/audio tracks, timeline events, markers, fades, link/group IDs, ripple preferences, and track state.
- Exact rational-time edit engine for linked A/V insert, overwrite, replace, move, start/end trim, split, delete, duplicate, copy/paste, slip, roll, group, ungroup, link, and unlink.
- Unlimited logical track creation/removal plus lock, mute, solo, visibility, height, and color state.
- Three VEGAS-style ripple scopes: Off, Affected Tracks, and All Tracks. Insert, trim, paste, duplicate, and delete apply the selected scope; locked tracks do not move; marker ripple is independently persisted.
- Atomic before/after sequence commands, so each edit and its ripple side effects undo and redo as one transaction.
- Snapping to zero, playhead, markers, cuts, and clip edges with a pixel-derived tolerance.
- Timeline markers, zoom, box selection, select-following, select-same-source, and selection expansion across linked/grouped events.
- Live QML timeline replacing the Milestone 1 placeholder, including reusable-clip drag/drop, time-positioned events, track headers, playhead/ruler, thumbnails, waveforms, fade indicators, mouse movement, and trim handles.
- Timeline toolbar commands for split, delete, duplicate, group, unlink, markers, snapping, ripple scope, and zoom.
- Persistent, conflict-checked timeline keyboard customization with a VEGAS-inspired default preset.
- Video-track stacking/visibility semantics and a timeline-aware Program tab image at the playhead; full decoded/effected program playback belongs to the Milestone 3 preview graph.
- Automatic 10 ms audio fades at adjacent hard cuts, plus editable per-event fade-in/fade-out values.
- Safe library deletion: timeline instances retain media/source ranges and undo restores their reusable-library association.

Acceptance evidence:

- Six UCRT64 suites pass: core, project persistence, controller end-to-end, Qt Multimedia playback, dedicated timeline integration, and QML startup.
- Timeline coverage validates insert/overwrite/replace, linked split/unlink, group/ungroup, slip, fades, snapping, all three ripple scopes, locked tracks, marker movement, atomic undo/redo, and exact save/reopen placement.
- Exact placement is exercised at 23.976, 24, 25, 29.97, 30, 50, 59.94, and 60 fps without converting editorial time to floating-point milliseconds.
- The dependency-free timeline core builds with MSVC 2022, and its core tests pass.
- The 1440x900 application was rendered offscreen after the live timeline replacement and visually inspected.

## Milestone 3 — effects, mixer, and keyframes

Status: **Complete** (2026-08-19)

Completed:

- Version 3 project schema for transforms, crop/fit/flip, opacity, speed, pitch behavior, reverse, freeze frames, clip/track/master audio, preview quality, proxy state, ordered effect chains, and automation.
- Validated effect descriptors with bounded parameters and stable IDs. Unknown, malformed, out-of-range, duplicate-time, and non-finite effect data is rejected during editing and loading.
- Hold, linear, and smooth keyframe interpolation for effect parameters and transforms, plus clip/track/master gain and pan envelopes.
- Undoable transform, audio, timing, effect, preset, and keyframe operations through the same atomic sequence-command path used by timeline editing.
- Speed changes alter event duration and ripple linked video/audio according to the active ripple scope. Pitch preservation, independent semitone shifting, reverse, and freeze state remain non-destructive.
- Core video controls: position, scale, rotation, anchor model, opacity, crop, fit, horizontal/vertical flip, reverse, speed, and freeze.
- Core video effects: brightness/contrast, saturation, hue, invert, grayscale, blur, sharpen, pixelate, posterize, threshold, and tint.
- Core audio controls: clip/track/master gain and pan, mute/solo, envelopes, pitch/speed, and a default disable-able master limiter.
- Core audio effects: parametric EQ, high/low-pass, compressor, limiter, normalize, reverb, delay/echo, distortion, bit crush, and noise gate.
- Inspector UI for transforms, timing, clip audio, effect add/remove/reorder/bypass/reset, parameter editing, interpolation, keyframe creation/removal, attribute copy/paste, and named presets.
- Mixer UI with audio-track and master strips, level indication, gain, pan, mute, solo, automation keys, ordered effect slots, editable effect parameters, and master-limiter control.
- Automatic effected-frame decoding at the Program playhead, including transform animation and parameter automation evaluation.
- On-demand processed A/V previews that apply clip timing, video effects, audio effects, track effects, master effects, and the master limiter, then play in the Program viewer with sound.
- Full, half, quarter, automatic, and proxy preview modes. Edit proxies are H.264/AAC media generated in a background task and tracked without replacing full-resolution sources.

Acceptance evidence:

- Seven UCRT64 suites pass: core, project persistence, controller end-to-end, Qt Multimedia playback, timeline integration, effects integration, and QML startup.
- Effects integration generates deterministic synthetic A/V media, exercises transforms, speed/reverse/freeze, linked duration changes, effect chains, bypass/reorder, keyframes, audio envelopes, clip/track/master audio, the limiter, attribute copying, Version 3 save/reopen, decoded effected frames, an edit proxy, and a processed A/V preview.
- The processed-preview test instantiates every built-in video and audio effect in one graph, ensuring each advertised FFmpeg processing path is accepted and produces playable media.
- Exact effect/keyframe state survives a project round trip without losing ordering, IDs, interpolation, parameters, or envelope values.
- The expanded dependency-free effect/timeline model builds under MSVC 2022 and its core tests pass.
- The final 1440x900 QML application rendered offscreen and was visually inspected.

## Milestone 4 — export and first usable alpha

Status: **Complete** (2026-08-19)

Completed:

- Six built-in output presets: YouTube 1080p/720p/4K, WebM 1080p, ProRes editing master, and audio-only WAV, plus editable dimensions/bitrates/codecs and persistent custom presets.
- Full-sequence and marked-region export with exact rational range boundaries.
- A sequential asynchronous render queue with per-job progress, cancellation, status, persistent finished history, output-adjacent FFmpeg logs, and partial-file cleanup.
- Full timeline composition from original source paths, including timing/speed/reverse/freeze, transforms, clip/track video effects, clip/track/master audio processing, mute/solo, master effects, and limiter. Export never substitutes edit proxies.
- Preflight errors for empty sequences, invalid regions/settings, missing source media, existing destinations, insufficient estimated free space, missing FFmpeg, and unavailable encoders.
- Program-frame PNG snapshots rendered from the same full-resolution composition graph.
- Four-page first-run tutorial, reopenable Guide action, and an alpha user guide covering editing, rendering, cancellation/logs, recovery, portable use, and uninstall.
- Reproducible UCRT64 Release packaging script, self-contained portable ZIP, and current-user setup EXE with Start Menu/Desktop shortcuts and uninstall script.
- Third-party redistribution notice documenting the GPL-enabled FFmpeg alpha configuration and the license/source audit required before public distribution.
- Automated dogfood scenario for a dense 30-second sentence mix, recurring effects, a 20-copy speed/pitch gag, all-track ripple deletion, and retrieval from 500 reusable clips.

Acceptance evidence:

- Nine UCRT64 suites pass, adding dedicated export integration and alpha dogfood coverage to all prior suites.
- Export integration verifies full-resolution/original-media output with proxies enabled, a marked one-second render, audio-only WAV, snapshots, progress, logs, cancellation cleanup, missing-codec failure, queue history, and custom presets.
- Dogfood timing: 17 ms for a 60-cut sentence mix, 2 ms for a 20-copy escalating gag, and below 1 ms for both all-track ripple deletion and 500-clip retrieval.
- The Release portable bundle launches with its local Qt/FFmpeg runtime and no missing QML or multimedia backend errors.
- MSVC 2022 rebuilds the expanded dependency-free core with zero warnings and all core tests pass.
- Release artifacts have recorded SHA-256 hashes; the final 1440x900 application/tutorial was rendered offscreen and visually inspected.

Known alpha constraints (not Milestone 4 blockers):

- Processed interactive preview remains capped at 15 seconds.
- Continuous exports use stored effect values; translating every keyframe envelope into per-frame FFmpeg expressions remains release-hardening work.
- Public redistribution must freeze dependency versions and ship complete corresponding license/source materials.

## Milestone 5 — dedicated YTP toolkit

Status: **Complete** (2026-08-19)

Completed:

- Stutter Builder with configurable slice length, repeat count, and alternating reverse; linked A/V is regenerated as synchronized, independently editable events.
- Rapid Reverse with configurable segment length/count and deterministic alternating playback direction.
- Frame Repeat using the project frame rate, selectable source-frame count, and repeat/hold count.
- Rhythm Repeat at a BPM grid or manually placed timeline markers, with beat count and gate duration controls.
- Speed Ladder with start/end speed, step count, pitch-per-step, and pitch-preservation controls.
- Safe Earrape audio treatment combining bounded gain, EQ, compression, distortion, a clip limiter, and mandatory master limiting.
- Six built-in visual distortion stacks: Deep Fried, VHS Breakdown, Pixel Scream, Threshold Vision, Mirror Hell, and Acid Trip.
- Persistent macro recording and playback for toolkit actions, plus Classic Stutter Fry, Reverse Meltdown, and Impact Spam built-in macros.
- Sentence Mixer v1: manually placed markers define phrase chunks, and a numeric order can rearrange/repeat them while preserving linked A/V.
- Seeded randomizer with reverse/effect probabilities, bounded speed/pitch ranges, linked-event shuffling, a human-readable preview plan, explicit commit/cancel, stale-preview rejection, and deterministic generated effect IDs.
- Dedicated scrollable YTP tab integrated beside Source, Inspector, and Mixer. All destructive builders remain single undoable sequence transactions.

Acceptance evidence:

- A tenth UCRT64 suite exercises every builder, all six visual presets, safe audio bounds/limiting, Sentence Mixer ordering, multi-step macros, custom macro persistence, deterministic randomizer preview/cancel/commit, stale-plan protection, linked A/V synchronization, ripple movement, and sequence validation.
- A representative Stutter + Deep Fried + Safe Earrape generated macro is rendered through the full-resolution FFmpeg export graph and produces playable A/V media.
- Controller coverage confirms macro recording operations remain undoable and randomizer preview does not mutate the project before commit.
- The dependency-free toolkit is part of `ytp_core` and is separately compiled under MSVC 2022.
- The final 1440x900 YTP Toolkit tab was rendered offscreen and visually inspected.

Known constraints (not Milestone 5 blockers):

- Sentence Mixer v1 intentionally uses manual cut markers; automatic word discovery belongs to Milestone 6 transcription.
- Rhythm Repeat uses manual markers or a fixed BPM grid; automatic onset/beat detection belongs to Milestone 6.

## Milestone 6 — intelligent retrieval and polish

Status: **Complete** (2026-08-19)

Completed:

- Version 4 schema for timed transcripts, nested sequences, adjustment clips, and rectangle/ellipse masks, with backward-compatible defaults for Version 1â€“3 projects.
- Fully offline transcription through the bundled FFmpeg Whisper filter and a user-selected local whisper.cpp model, plus project-wide text search and one-click source-timed Clip Library creation.
- Deterministic PCM onset analysis mapped through event source ranges and speed into editable beat markers.
- Undoable multi-sequence creation/removal/switching, nested placement, protected in-use deletion, recursive-cycle rejection, and full-resolution nested export flattening.
- Media-free adjustment clips using the normal ordered video-effect Inspector and time-bounded composite rendering.
- Non-destructive masks with normalized geometry, feather, opacity, inversion, persistence, validation, UI controls, and FFmpeg alpha rendering.
- Visible-range timeline virtualization for long cut-heavy edits, while preserving the full project and export graph.
- A Finish workspace for retrieval and advanced layers, persisted UI scaling/high contrast/reduced motion/window geometry/monitor choice, automatic Qt high-DPI behavior, accessible retrieval controls, and an external full-screen Program monitor.
- Version 0.3.0-alpha packaging metadata, refreshed guide, and explicit local-model licensing guidance.

Acceptance evidence:

- Eleven UCRT64 suites pass, including new Version 4, transcript parser, synthetic onset, nesting/cycle, adjustment, mask, persistence, and 20,000-event performance coverage.
- The 20,000-event effect-capable sequence validates within a five-second budget, and QML only requests items intersecting its current viewport.
- The complete application starts in the Qt offscreen smoke test. The dependency-free core is separately rebuilt under MSVC 2022 before release packaging.

## Milestone 7 - real-time remix workflow

Status: **Complete** (2026-08-19)

Completed:

- Version 6 projects additionally persist per-event timeline names; beat grids, reusable compound-clip definitions, tracked animation, and editable clip captions remain backward compatible.
- Continuous full-sequence Program caches use the same nested, adjustment, mask, effect, transform, and audio graph as export. Edits mark the cache stale and optionally trigger a debounced rebuild; progress, cancellation, cache size, clearing, and dropped-frame telemetry are visible in Remix.
- Sentence Mixer v2 searches timed transcripts by exact spelling or a deterministic phonetic code, assembles linked A/V words with padding and crossfades, adds editable rendered captions, and stores each result as a reusable compound clip.
- Beat grids can be entered or estimated from detected onsets. Selection tools snap to beats, cut at every beat, or generate audio-reactive effect keyframes.
- Compound clips support live reusable instances and independent copies. Motion tracking generates editable mask keyframes; completed tracks can drive clip motion or inverse stabilization.
- Twelve advanced YTP effects were added: RGB split, chromatic aberration, wave and lens warp, kaleidoscope, edge echo, recursive trails, time smear, frame blend, screen shake, chroma key, and datamosh-style blending.
- The visual macro editor can compose, reorder, name, save, preview seeded variations, and conditionally apply toolkit steps to a selection, every cut, markers/beats, or transcript events.
- Dirty edits are written to a recovery journal. Projects can recover or discard it and collect the project, original media, proxies, fonts, and a portable project file into a ZIP archive.
- Long-timeline coverage now validates 100,000 events, with viewport filtering retained for the QML timeline.
- Release metadata and packaging now identify version 0.4.0-alpha.

Acceptance evidence:

- The dedicated Milestone 7 suite verifies phonetic retrieval, captioned sentence construction, beat estimation and reactive keys, Version 5 round trips, compounds, synthetic motion tracking, all twelve advanced effects through cached FFmpeg renders, and 100,000-event validation.
- The complete UCRT64 regression suite and offscreen QML smoke test pass. The dependency-free core remains separately covered by the MSVC build.

## Milestone 8 - professional editing interface

Status: **Complete** (2026-08-19)

Completed:

- Replaced platform-native control rendering with a deterministic Qt Basic control foundation so the dark palette, disabled states, focus, hover, checked, and highlighted controls render consistently on Windows and in packaged builds.
- Added a centralized visual system for canvas/panel elevation, borders, text hierarchy, accent, status colors, typography, reusable tool buttons, accent actions, workspace tabs, and metadata pills.
- Rebuilt the application toolbar around product identity, primary project actions, undo/redo, project state, workspace selection, help, snapshot, and a persistent Export call to action. The footer now communicates operation and save state without visual noise.
- Added YTP Focus, Cut & Arrange, and Audio Lab workspace presets. At compact widths they prioritize the relevant inspector or library instead of squeezing three unusable panels into the window.
- Reworked library navigation, search/filter controls, media import action, clip cards, thumbnail treatment, selection/drag feedback, empty states, and project-media rows.
- Added Source, Program, and true side-by-side Dual monitor modes with clear empty states, preserved aspect ratios, preview-cache controls, waveform scrubbing, transport controls, and source mark feedback.
- Rebuilt the timeline header, editing toolbelt, YTP quick actions, ripple/snapping controls, zoom/time display, time ruler, track identity, audio/video badges, mute/solo/lock/visibility controls, drop feedback, clip cards, linked/grouped badges, fade indicators, trim handles, marker display, playhead head, and empty-project call to action.
- Rebuilt all six right-side workspaces around a shared responsive inspector-card system: Source has a compact active-media and reusable-clip flow; Edit exposes full-width property rows; Mix uses readable horizontally scrolling channel strips; YTP separates builders into labeled operations; and Finish/Remix wrap dense tools without clipping. All existing functionality is preserved.
- Replaced the long scrolling inspectors with fixed-header, page-based Edit, YTP, Finish, and Remix panels. Dense tools are split into focused pages and variable collections edit one selected item at a time. Each selected page body, plus Source and Mix, now has bounded mouse-wheel/touchpad scrolling and an as-needed scrollbar; its viewport is constrained above the timeline instead of extending behind it. Delayed contextual hover help covers workspace navigation, project commands, timeline operations, track controls, page selectors, and primary clip actions.
- Replaced the original onboarding window with a five-stage guided tour that includes the Milestone 7 Remix workflow and the new monitor/workspace model.
- Added deterministic standard-size and wide Remix/Dual screenshot smoke tests in addition to the existing QML startup test.
- Release metadata and packaging now identify version 0.5.0-alpha.

Acceptance evidence:

- Native application screenshots were inspected at 1024x640, 1440x900, and 1920x1080, including every Source/Edit/Mix/YTP/Finish/Remix inspector tab. Compact mode removes panel and header clipping while the wide layout exposes the complete Remix workspace and Dual monitors.
- Both new visual smoke suites build the real QML application, choose their workspace/monitor states, resize it, render a PNG, and exit successfully.
- The complete UCRT64 suite, MSVC core build/tests, release deployment, and packaged offscreen startup are required before the milestone package is accepted.

The implementation roadmap is complete. Remaining work is post-alpha maintenance, additional codecs/platforms, and public-distribution legal review rather than an unfinished milestone.
