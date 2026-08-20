# YTP Editor — Product and Build Plan

## 1. Product goal

Build a Windows-first, non-destructive desktop video editor designed around the way YouTube Poop is actually made: find a useful moment, turn it into a reusable fragment immediately, repeat and rearrange it quickly, distort its audio/video, and keep experimenting without losing earlier work.

The editor should feel faster than a general-purpose NLE for joke construction while retaining the timeline, track controls, ripple behavior, preview, and dependable export expected from VEGAS Pro.

Working name: **YTP Editor**. The name and visual identity can change later.

## 2. Product principles

1. **A selected range becomes a reusable clip instantly.** Creating a library clip never copies or re-encodes media; it stores source in/out references.
2. **Editing stays non-destructive.** Source files are never modified. Every operation can be undone.
3. **Keyboard-first, mouse-friendly.** Frequent actions take one key or one drag; all shortcuts are remappable.
4. **Audio is not an afterthought.** Waveforms, pitch, speed, envelopes, effects, and track mixing are part of the main editing flow.
5. **YTP transformations are first-class commands.** Reverse, stutter, sentence-mix, freeze, pitch/speed changes, and common visual distortions should not require complicated effect chains.
6. **Preview can be cheap; export must be correct.** Proxy media and adjustable preview quality keep interaction responsive, while final render uses full-resolution sources.
7. **Experiments should be cheap.** Autosave, deep undo, presets, effect copy/paste, duplication, and versioned sequences encourage destructive-looking but reversible edits.

## 3. Primary workflow

### Source-to-library loop

1. Import an infomercial or other source.
2. Open it in the Source Viewer.
3. Scrub with visible audio waveform and frame stepping.
4. Set an In point and Out point.
5. Press **Create Clip** (default shortcut: `C`).
6. A named, thumbnail-backed library item appears immediately.
7. Drag that item to any timeline position as many times as needed, or use insert/overwrite shortcuts.

Each library item stores source media ID, in/out time, name, tags, notes, thumbnail, color, and favorite status. Multiple timeline uses refer to the same library item but have independent effects and trims.

### Timeline joke-building loop

1. Select or split a fragment.
2. Duplicate, repeat, reverse, freeze, speed-shift, pitch-shift, or stutter it from a quick-action strip or shortcut.
3. Audition immediately at reduced preview quality if necessary.
4. Save the result as a preset, compound clip, or new library fragment when useful.
5. Insert/remove material with configurable ripple scope.

## 4. Screen layout

- **Left:** project media and reusable Clip Library, with grid/list views, search, tags, favorites, bins, and recently used items.
- **Center top:** Source Viewer and Program Viewer tabs; optional side-by-side mode.
- **Right:** Inspector with Transform, Speed, Audio, Effects, Keyframes, and Properties sections.
- **Bottom:** multitrack timeline with video/audio tracks, waveform and thumbnail display, markers, snapping, and zoom controls.
- **Bottom/right dock:** audio mixer with per-track meters, mute, solo, gain, pan, and effect slots.
- **Command palette:** searchable access to every edit/effect, including shortcuts.

Layouts are dockable and saveable. A compact single-monitor layout is the initial priority.

## 5. Feature scope

### 5.1 MVP — must be solid before expanding

#### Project and media

- New/open/save project; autosave and crash recovery.
- Import common video, audio, and still-image formats supported by the bundled media engine.
- Missing-media relink and portable relative paths where possible.
- Background generation of thumbnails, audio waveforms, and proxy media.
- Project settings for resolution, frame rate, sample rate, and preview quality.
- Project Media bins plus the separate reusable Clip Library.

#### Source Viewer and Clip Library

- Play/pause, seek, shuttle, frame-step, waveform scrubbing, In/Out points, and selection duration.
- One-command **Create Library Clip from Selection**.
- Rename, tag, color, favorite, annotate, sort, search, and filter clips.
- Drag a library clip to the timeline repeatedly without duplicating source media.
- Insert at playhead and append-to-end commands.
- Update a library clip's source range without unexpectedly changing existing timeline instances; offer an explicit “update instances” action.
- Recently created and recently used sections.

#### Timeline

- Unlimited logical video and audio tracks, subject to machine performance.
- Linked video/audio by default; temporary or permanent unlinking.
- Select, move, trim, split, delete, duplicate, copy/paste, box-select, and group.
- Snapping to playhead, markers, cuts, clip edges, and selection bounds.
- Insert, overwrite, and replace edits.
- Slip and roll trims; slide trim can follow after MVP if necessary.
- Track visibility, lock, mute, solo, height, and color controls.
- Video thumbnails and scalable audio waveforms.
- Timeline and source markers with labels/colors.
- Optional automatic short audio crossfades at hard cuts.

#### VEGAS-style auto ripple

Ripple is an explicit toolbar mode with three scopes:

1. **Off:** only the edited item changes; gaps/overlaps may result.
2. **Affected tracks:** downstream clips on edited tracks move by the duration delta.
3. **All tracks:** all downstream events move, preserving sync across the sequence.

Insert, trim, paste, speed-duration changes, and delete obey the selected scope. A separate **ripple delete** closes a selected gap or removed range. Locked tracks never move. Markers/regions have a preference controlling whether they ripple. Every ripple operation is represented as one undoable transaction and is covered by dedicated tests.

#### Core video controls and effects

- Position, scale, rotation, anchor, opacity, crop, and aspect-fit/fill.
- Horizontal/vertical flip and mirror.
- Reverse selected timeline clips.
- Constant speed from slow motion through fast motion, with optional pitch preservation.
- Freeze frame/frame hold and ping-pong/boomerang.
- Keyframes with hold, linear, and smooth interpolation.
- Fade/crossfade and basic compositing over multiple tracks.
- Brightness/contrast, saturation, hue shift, invert, grayscale, blur, sharpen, pixelate, posterize, threshold, and simple color tint.
- Text/title cards with font, outline, shadow, position, and animation.

#### Core audio controls and effects

- Clip gain plus per-track gain, pan, mute, solo, peak meter, and master meter.
- Clip fades and keyframeable volume/pan envelopes.
- Speed with pitch preserved, and pitch shift with duration preserved.
- Reverse audio with its linked video or independently after unlinking.
- Parametric EQ, high/low-pass filters, compressor, limiter, normalize, reverb, delay/echo, distortion, bit crush, and noise gate.
- Effect bypass, reorder, presets, copy/paste, and reset.
- Master limiter enabled by default to prevent accidental clipping during extreme YTP effects; it can be disabled deliberately.

#### Export

- MP4/H.264 + AAC YouTube presets at minimum, plus an editable custom preset.
- Render whole sequence or marked region.
- Background render queue with progress, cancellation, clear error messages, and render log.
- Full-resolution sources used even when preview proxies are enabled.
- Save a project snapshot with each render for reproducibility.

### 5.2 YTP toolkit — first expansion after the core is dependable

- **Stutter Builder:** repeat the selected range by count or target duration.
- **Sentence Mixer:** split at playhead/markers, duplicate syllable-size fragments, and rearrange fragments quickly.
- **Rhythm Repeat:** repeat a fragment on a grid or detected beat markers.
- **Rapid Reverse:** alternate forward/reverse copies for a configurable count.
- **Frame Repeat:** hold or repeat one frame while audio optionally continues.
- **Randomizer:** seeded shuffle of selected fragments, with minimum/maximum slice length and an undoable preview.
- **Speed Ladder:** generate ascending/descending speed or pitch copies.
- **Earrape preset:** gain/distortion/compression chain with a loudness warning and limiter safety.
- **Visual presets:** RGB split/chromatic aberration, shake, fisheye, swirl, kaleidoscope, glow, trails/echo, scanlines, VHS noise, and configurable glitch.
- **Effect macros:** one shortcut applies a saved multi-effect chain to selected clips.
- **Paste attributes:** choose transform, speed, audio, effects, and keyframes independently.
- **Render-and-replace / bake:** cache expensive effects while retaining the original edit underneath.

### 5.3 High-value later features

- Speech-to-text transcript for source media; search spoken words and create clips directly from transcript ranges.
- Phoneme/syllable suggestions for sentence mixing. Suggestions remain user-controlled and never alter the edit automatically.
- Beat/onset detection and automatic markers.
- Nested sequences/compound clips.
- Adjustment clips and track-level video effects.
- Masks with tracking; motion tracking comes after static/keyframed masks.
- Blend modes, chroma key, and luma key.
- Multicam is low priority for YTP use.
- Image-sequence and transparent-alpha export.
- Custom script/macro API after the project format stabilizes.
- Datamosh as an experimental offline/bake effect only; it should not destabilize normal preview or export.

## 6. Interaction details that will save time

- `I` / `O`: set source In/Out.
- `C`: create library clip from source selection.
- `S`: split selected/targeted clips at playhead.
- `R`: reverse selected clip(s).
- `D`: duplicate selection immediately after itself, respecting ripple mode.
- `Ctrl+D`: repeat dialog for count, spacing, and alternation.
- `F`: freeze at playhead.
- `Q` / `W`: ripple trim start/end to playhead.
- `[` / `]`: trim selected edge to playhead.
- `J K L`: reverse/pause/forward shuttle; repeated presses increase speed.
- Mouse wheel zooms the timeline with a modifier and scrolls otherwise.
- Dragging an effect preset onto a selection applies it to every selected clip.
- A/B preview temporarily bypasses selected effects without changing the project.
- “Select all following” and “select same library source” are first-class commands.

All defaults are proposals and will be remappable. A VEGAS-inspired shortcut preset should be included to reduce retraining.

## 7. Technical architecture

### Recommended stack

- **Application/UI:** C++ with Qt 6 and QML, targeting Windows 10/11 x64 initially.
- **Editing/playback/render engine:** MLT Framework through its C++ wrapper.
- **Media decode/encode:** FFmpeg-backed MLT services.
- **Audio DSP:** MLT/LADSPA/LV2-compatible filters where redistributable; small missing effects implemented as internal DSP modules.
- **Project database:** human-readable versioned JSON for editorial state, with a small SQLite cache for thumbnails, waveforms, media analysis, and search indexes.
- **Build/test:** CMake, CTest, Qt Test, and a packaged fixture set containing short synthetic media.
- **Packaging:** self-contained Windows installer and portable development build.

This uses a mature multitrack composition engine instead of writing seeking, decoding, synchronization, filter graphs, and encoding from zero. MLT already models producers, per-track playlists, multitracks, filters, transitions, keyframed properties, preview scaling, and headless rendering. Qt provides a supported Windows desktop UI stack.

### Important architectural boundary

The saved project is **our model**, not raw MLT XML. The application converts the project model into an MLT graph for preview/export. This keeps user-facing concepts—library clips, ripple modes, groups, linked A/V, macros, and future migrations—under our control and makes projects testable without running the media engine.

### Core modules

```text
Qt/QML UI
  ├─ Source Viewer + Clip Library
  ├─ Timeline + editing commands
  ├─ Inspector + effect controls
  └─ Mixer + render queue
            │
Command/Undo layer (all edits are transactions)
            │
Project model ── Cache/index service
            │
MLT graph adapter
  ├─ Preview transport
  ├─ Proxy/full-res source resolver
  ├─ Video/audio effects
  └─ Headless export jobs
```

The command layer is essential: ripple edits, group moves, repeats, and effect macros must either complete fully or leave the model unchanged. It also provides deterministic undo/redo and makes timeline behavior unit-testable.

## 8. Project data model

All IDs are stable UUIDs. Time is stored as rational time (frame/sample-aware), not floating-point seconds.

- **Project:** format version, settings, media, library items, sequences, presets, UI state.
- **MediaAsset:** path, fingerprint, streams, duration, metadata, proxy/cache references.
- **LibraryClip:** media asset ID, source in/out, display metadata, tags, thumbnail time.
- **Sequence:** settings, ordered tracks, markers, master effect chain.
- **Track:** kind, order, lock/mute/solo/visibility state, effects, ordered timeline items.
- **TimelineItem:** library/media reference, sequence start, source range, speed map, link/group IDs, transform, fades, effect chain.
- **EffectInstance:** stable effect type ID, enabled state, parameter values, keyframes, preset provenance.
- **EditCommand:** serializable before/after mutation metadata for undo, recovery, and debugging.

Library items and timeline items are deliberately separate. Editing a timeline instance cannot corrupt the reusable source definition.

## 9. Performance strategy

- Decode and media analysis run off the UI thread.
- Waveform/thumbnails are generated incrementally and cached by media fingerprint.
- Preview resolution can be Full, 1/2, 1/4, or automatic.
- Optional edit-friendly proxies are generated in the background and transparently replaced by originals during render.
- Ahead-of-playhead frame/audio buffering is bounded by memory settings.
- Effect graphs cache unchanged segments; expensive temporal effects can be baked.
- Timeline rendering uses viewport virtualization so a long project does not create one UI object per off-screen clip.
- Scrubbing prioritizes latency; normal playback prioritizes A/V synchronization.

Target budgets for the first release on a reasonable midrange PC:

- UI response to an edit command: under 50 ms excluding media decode.
- Cached timeline redraw: under 16 ms at ordinary zoom levels.
- Source In/Out to library item: under 100 ms, with thumbnail completion asynchronous.
- Autosave: no visible playback interruption.

## 10. Reliability, safety, and licensing gates

- Autosave uses atomic replacement and keeps rotating recovery copies.
- Save format migrations are one-way only after creating a backup copy.
- Missing/broken effects remain visible as disabled placeholders rather than being discarded.
- Source fingerprints detect moved or replaced files during relink.
- Loud audio chains display clipping/peak warnings; final output can retain a true-peak limiter.
- Crash reports and logs never include source media unless the user explicitly opts in.
- Before public distribution, audit Qt, MLT, FFmpeg, codecs, and every effect plugin for license and redistribution obligations. Select LGPL-compatible configurations where practical and document source/license notices. This is a release blocker, not cleanup work.

## 11. Implementation milestones

### Milestone 0 — foundations

- Repository, CMake build, formatting/linting, tests, CI, dependency packaging, app shell.
- Spike: decode and display one video through MLT; seek accurately; play synchronized audio; render a trimmed range.
- Spike acceptance gate: repeated seeks and a 10-minute playback test do not deadlock or drift materially.

### Milestone 1 — project, source viewer, and Clip Library

- Project model and versioned JSON persistence.
- Media import, metadata, thumbnail/waveform cache.
- Source playback, frame stepping, In/Out selection.
- Create/search/tag/favorite library clips and drag them into a provisional timeline.
- Autosave, recovery, missing-media relink, undo/redo foundation.

**User-visible outcome:** import a long infomercial, harvest dozens of named fragments, close/reopen, and retain the library.

### Milestone 2 — dependable multitrack timeline

- Video/audio tracks, linked events, move/trim/split/delete/duplicate, snapping, markers, zoom, waveforms.
- Insert/overwrite, fades, basic compositing, track controls.
- Ripple Off/Affected Tracks/All Tracks plus ripple delete.
- Keyboard customization and VEGAS-style preset.

**Acceptance gate:** a scripted edit suite validates placement and duration after every operation at several frame rates. No ripple behavior ships based only on manual testing.

### Milestone 3 — effects, mixer, and keyframes

- Transform, crop, opacity, speed, reverse, freeze, core color effects.
- Clip/track/master audio control, meters, envelopes, pitch/speed, EQ, compressor, limiter, reverb, delay, distortion.
- Effect chain UI, presets, copy/paste attributes, keyframe editor.
- Preview quality/proxy workflow.

**User-visible outcome:** complete a small real YTP edit with no external audio editor.

### Milestone 4 — export and first usable alpha

- YouTube render presets, custom settings, marked-region render, render queue/log.
- Full-resolution/proxy correctness tests, cancellation, disk-space and missing-codec errors.
- Installer, portable build, crash recovery exercises, first-run tutorial.
- Dogfood by recreating a short existing YTP sequence and log every slow interaction.

### Milestone 5 — dedicated YTP toolkit

- Stutter Builder, Rapid Reverse, Frame Repeat, Rhythm Repeat, Speed Ladder, and safe Earrape preset.
- Visual distortion preset pack and macro system.
- Sentence Mixer v1 using manually placed cuts/markers.
- Seeded randomizer with preview/commit.

### Milestone 6 — intelligent retrieval and polish

- Optional local transcription, word search, transcript-to-library clipping.
- Beat/onset markers, nested sequences, adjustment clips, masks.
- Performance profiling on long and effect-heavy projects.
- Accessibility, high-DPI, multi-monitor layout, documentation, and release hardening.

## 12. Verification plan

### Unit tests

- Rational time conversion at 23.976, 24, 25, 29.97, 30, 50, 59.94, and 60 fps.
- Every trim/insert/delete operation under all ripple scopes.
- Locked-track behavior, group/link behavior, marker ripple preferences.
- Reverse and speed mapping between sequence time and source time.
- Project migrations, undo/redo round trips, autosave recovery.

### Integration tests

- Import → select range → create library clip → use it five times → save/reopen.
- Mixed-frame-rate and mixed-sample-rate media.
- Proxy preview followed by full-resolution export.
- Audio/video sync after split, reverse, speed, ripple, and nested effects.
- Missing file relink and media replacement detection.
- Render cancellation and recovery from encoder errors.

### Golden-media tests

Render short deterministic fixtures and compare frame hashes, durations, timestamps, and audio characteristics within documented tolerances. Include reverse, freeze, pitch shift, reverb tail, crossfade, compositing, and keyframes.

### Dogfood scenarios

- Build a 30-second sentence-mix sequence from a single infomercial.
- Create a 20-copy escalating speed/pitch gag in under one minute.
- Remove a middle section with all-track ripple while music/effects remain synchronized.
- Find and reuse any saved fragment from a library of 500 items in a few seconds.

## 13. Explicit non-goals for the first release

- Competing with VEGAS/Premiere/Resolve across every professional workflow.
- Collaborative cloud editing or account infrastructure.
- Mobile/web versions.
- Camera RAW, advanced HDR finishing, 3D scenes, or full motion-graphics compositing.
- Plugin SDK compatibility with every commercial VST/OFX plugin in v1.
- AI-generated edits that make timeline changes without an explicit preview and commit.

## 14. First development slice

The first implementation slice should prove the unique workflow before polishing the entire NLE:

1. Build the Qt/MLT playback spike.
2. Define and test the rational-time project model.
3. Import one source and show its waveform/thumbnails.
4. Mark In/Out and create a Clip Library item.
5. Drag the item onto a single A/V timeline multiple times.
6. Split, duplicate, reverse, and ripple-delete instances.
7. Preview and render the result.

If this vertical slice is responsive and frame-accurate, extend it into the milestones above. If MLT cannot meet the seek/preview requirements on the target Windows machines, stop after the spike and reassess the engine rather than building the rest of the UI around it.

## 15. Architecture references

- MLT framework overview and multitrack/filter architecture: https://www.mltframework.org/docs/framework/
- MLT project serialization concepts: https://www.mltframework.org/docs/mltxml/
- MLT documentation index, preview scaling, property animation, and headless rendering: https://www.mltframework.org/docs/
- Qt supported desktop platforms: https://doc.qt.io/qt-6/supported-platforms.html

