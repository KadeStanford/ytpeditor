# YTP Editor alpha guide

## Your first edit

1. Import a source infomercial. The first import automatically places the full linked video/audio source on V1/A1 at 0:00 and also creates a reusable full-source library clip.
2. For smaller reusable moments, set In and Out with `I` and `O`, press `C`, then drag the saved fragment onto a track. Split with `S`, duplicate with `D`, and choose Off, Affected Tracks, or All Tracks ripple behavior in the header.
3. Select a timeline event to edit reverse, speed, pitch, transform, audio, effects, and keyframes in Inspector. Use Mixer for track and master audio.
4. Build an effected A/V preview from the Program viewer. Preview quality may use a proxy; export always reads the original media.
5. Open Export, choose a YouTube/WebM/master/audio preset or edit custom dimensions and bitrates, optionally choose a marked millisecond range, and add it to the queue.

## Render queue and recovery

Each job reports progress and can be cancelled without leaving a partial destination file. A `.render.log` sits next to every attempted output. Finished queue history survives restart. The editor rejects missing media, an unavailable codec, an existing destination, invalid regions, and estimates insufficient free space before launch.

Project autosaves rotate through three recovery files. On restart, accept the recovery prompt to load a newer autosave. Media relinking validates replacement duration before changing the project.

## Dedicated YTP toolkit

Select one timeline event and open the **YTP** tab beside Inspector and Mixer. Linked video and audio are handled together.

- **Stutter**, **Rapid Reverse**, and **Frame Repeat** replace the selected event with editable micro-events.
- **Rhythm Repeat** adds gated copies from the playhead on a BPM grid or at manually placed markers.
- **Speed Ladder** makes sequential copies with escalating speed and optional pitch steps.
- **Safe Earrape** applies bounded gain, EQ, compression, distortion, and both clip/master limiting.
- The distortion pack applies reusable Deep Fried, VHS, pixel, threshold, mirror, and acid visual stacks.
- For **Sentence Mixer**, put markers inside the selected phrase. Enter chunk indices such as `2,0,1,1` to rearrange and repeat the marked chunks.
- **Seeded Randomizer** shows the exact planned shuffle, reverse, speed, pitch, and effects before anything changes. Commit the plan explicitly or cancel it; the same seed/settings produce the same plan.
- Select **Record Macro**, run any sequence of toolkit actions, name it, and save it. Custom macros persist alongside the three built-in macros.

Each builder or macro commits as one undoable timeline edit. Randomizer preview never enters undo history until committed.

## Intelligent retrieval and advanced layers

Open **Finish** for Milestone 6 tools:

- **Offline Transcription** asks for a local whisper.cpp `.bin` model and runs it through the bundled FFmpeg Whisper filter. Nothing is uploaded. Search results retain exact source timing; **Clip** saves a padded result directly into the reusable Transcript bin. Models are not distributed with the editor.
- **Detect Beats** analyzes the selected event's decoded mono audio and adds cyan onset markers mapped through its source range and speed.
- Create and open multiple sequences, then nest any non-active sequence on a video track. Recursive nesting is rejected. A sequence that is in use cannot be deleted.
- An **Adjustment Clip** has no source media: add video effects to it in Inspector to process the composite during that time range.
- Add rectangle or ellipse masks in Inspector, change normalized position/size, feather, opacity, or inversion, and render them non-destructively.
- Display settings persist UI scale, high contrast, reduced-motion preference, window geometry, monitor choice, and an optional full-screen external Program monitor.

Timeline event models are limited to the visible time range while scrolling. This keeps interaction responsive on long, cut-heavy projects without removing events from the project or export graph.

## Real-time remix workspace

Open **Remix** for Milestone 7 tools:

- Enable **Continuous** in Program to build a low-resolution cache of the complete active sequence. Editing marks it stale and schedules a fresh cache; task progress and cancellation are shown in Remix.
- Search transcript words with **Phonetic** enabled to include similar-sounding spellings. Add results in any order, adjust word padding/crossfade, and build a reusable captioned sentence at the playhead.
- Enter or estimate a beat grid, then snap selected clips, cut them at every grid point, or generate beat-driven shake keys.
- **Compound Selection** turns selected events into a reusable nested clip. Insert **Live** to share later compound edits or **Copy** for an independent version.
- Draw a mask around a feature and choose **Track**. After tracking completes, **Attach Clip** follows the motion and **Stabilize** applies its inverse. All generated mask and transform keys remain editable.
- Build macros visually, preview deterministic seeded variations, and apply them conditionally to the selection, every cut, markers/beats, or transcript events.
- The recovery journal is independent of rotating autosaves and records dirty edits after 750 ms. **Collect Project** creates a ZIP containing a rewritten project plus media and available proxies.

Sentence captions can be enabled, edited, resized, or recolored in Inspector. The advanced effects list there also includes RGB split, chromatic aberration, warps, trails/smears, shake, chroma key, and datamosh-style treatments.

## Workspaces and interface

The header's workspace selector changes both focus and compact-window behavior:

- **YTP Focus** prioritizes the YTP/Remix inspector and keeps reverse, stutter, and shake actions directly above the timeline.
- **Cut & Arrange** prioritizes the Clip/Media library for source selection and timeline assembly.
- **Audio Lab** prioritizes the mixer and audio inspector while giving the right panel more room.

Source and Program can be viewed separately or together with **Dual** on displays at least 1400 pixels wide. At narrow window sizes, the selected workspace hides the less important side panel instead of crushing both; switching workspace immediately exposes the other workflow.

The Edit, YTP, Finish, and Remix tabs use page-based inspectors. Choose a tool page immediately below the heading; only the selected page body scrolls when its controls exceed the available height. Source and Mix use the same reliable mouse-wheel/touchpad path, with scrollbars shown only when needed. Position/Crop and Stutter/Rapid Reverse/Frame Repeat are separate compact pages, while variable collections such as effects, sequences, compounds, and macro steps are edited one selected item at a time. Hover over abbreviated and high-impact controls—workspace tabs, timeline tools, track M/S/lock/visibility, page selectors, and primary actions—to see contextual help.

Timeline events use their library color, video thumbnail or waveform, and badges for linked/grouped state. Pink outlines indicate selection. The left and right white handles trim an event, the yellow lower bars show fades, and the pink vertical line is the playhead. Use the timeline toolbelt for split, delete, duplicate, group, markers, snapping, ripple, reverse, stutter, shake, and zoom.

## Portable and installed alpha

The portable ZIP runs in place after extraction. The setup EXE installs for the current user under `%LOCALAPPDATA%\Programs\YTPEditor` and creates Start Menu/Desktop shortcuts. Run `uninstall-alpha.ps1` from the installed folder to remove the alpha and its shortcuts. Project and media files are never removed by uninstall.
