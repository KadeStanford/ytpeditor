import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import YTPEditor.Native 1.0

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: projectController.projectName + (projectController.dirty ? " *" : "") + " - YTP Editor"
    color: uiSettings.highContrast ? "#000000" : theme.canvas
    font.family: "Segoe UI Variable"
    font.pixelSize: Math.round(13 * uiScale)
    property real uiScale: uiSettings.uiScale
    readonly property var appTheme: theme

    Settings { id: uiSettings; property real uiScale: 1.0; property bool highContrast: false; property bool reducedMotion: false; property int monitorIndex: 0; property int savedX: 80; property int savedY: 60; property int savedWidth: 1440; property int savedHeight: 900; property int workspaceMode: 0; property bool libraryCollapsed: false; property bool inspectorCollapsed: false }
    Component.onCompleted: { width=Math.max(minimumWidth,uiSettings.savedWidth);height=Math.max(minimumHeight,uiSettings.savedHeight);x=Math.max(Screen.virtualX,uiSettings.savedX);y=Math.max(Screen.virtualY,uiSettings.savedY);root.markOutMs=projectController.sourceDurationMs;root.refreshTimelineContentWidth() }
    onClosing: function(close) {
        root.captureSessionState()
        uiSettings.savedX=x;uiSettings.savedY=y;uiSettings.savedWidth=width;uiSettings.savedHeight=height
        if (projectController.dirty && !root.allowClose) {
            close.accepted = false
            projectController.flushRecoveryJournal()
            root.pendingDestructiveAction = "close"
            unsavedChangesDialog.open()
        }
    }

    Window { id: externalMonitor; title:"YTP Editor — External Program Monitor"; width:960;height:540;color:"black";screen:Qt.application.screens[Math.min(uiSettings.monitorIndex,Qt.application.screens.length-1)]
        Image { anchors.fill:parent;source:timelineController.programImageUrl;fillMode:Image.PreserveAspectFit }
        Label { anchors.centerIn:parent;visible:!timelineController.programImageUrl;text:"Program monitor";color:"white" }
    }

    property int markInMs: 0
    property int markOutMs: 0
    property string editingClipId: ""
    property string editingTimelineItemId: ""
    property real shuttleRate: 0
    property string exportPath: ""
    property int tutorialPage: 0
    property var sentenceWords: []
    property int programFrames: 0
    property int programDroppedFrames: 0
    property real lastProgramFrameTimestampUs: -1
    property string countedPreviewUrl: ""
    property int trackHeaderWidth: 124
    property real timelineContentWidth: 1024
    property bool playProgramWhenReady: false
    property bool resumeProgramAfterSeek: false
    property real pendingProgramPosition: -1
    property bool programPlayerUpdatingPlayhead: false
    property bool instantPreviewActive: false
    property bool instantPlaybackRequested: false
    property bool programPlaybackRequested: false
    property bool instantPlayerUpdatingPlayhead: false
    property real pendingInstantPosition: -1
    property real timelineClockLastTimestamp: 0
    property string timelineClockItemId: ""
    property bool timelineClockUpdatingPlayhead: false
    property bool timelineClockPreviewExact: false
    property bool timelineScrubbing: false
    property bool resumeAfterTimelineScrub: false
    property real pendingTimelineScrubMs: -1
    property bool marqueeActive: false
    property real marqueeStartX: 0
    property real marqueeStartY: 0
    property real marqueeCurrentX: 0
    property real marqueeCurrentY: 0
    readonly property bool focusProgramMode: false
    readonly property int activeViewerIndex: focusProgramMode ? 1 : viewerTabs.currentIndex
    property bool timelineDragActive: false
    property real timelineDragDeltaPx: 0
    property bool libraryClipDragActive: false
    property real libraryClipDragX: 0
    property real libraryClipDragY: 0
    property string libraryClipDragId: ""
    property string libraryClipDragName: ""
    property bool timelineFollowSuppressed: false
    property int inspectorContentIndex: 1
    property bool viewerOptionsPinned: false
    property string effectBrowserFilter: "All"
    property bool smoothDraftPlayback: true
    property bool allowClose: false
    property string pendingDestructiveAction: ""
    property bool continueAfterSave: false

    Rectangle {
        id: libraryClipDragProxy
        objectName: "libraryClipDragProxy"
        property string clipId: root.libraryClipDragId
        parent: root.contentItem
        visible: root.libraryClipDragActive
        x: root.libraryClipDragX - width / 2
        y: root.libraryClipDragY - height / 2
        width: 190
        height: 58
        radius: 4
        z: 10000
        color: theme.panelHover
        border.color: theme.accent
        border.width: 2
        opacity: .94
        Drag.active: root.libraryClipDragActive
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        Drag.supportedActions: Qt.CopyAction
        Drag.proposedAction: Qt.CopyAction
        Drag.keys: ["application/x-ytp-library-clip"]
        Drag.mimeData: {
            "application/x-ytp-library-clip": root.libraryClipDragId,
            "text/plain": root.libraryClipDragName
        }
        Label {
            anchors.fill: parent
            anchors.margins: 8
            text: root.libraryClipDragName
            color: theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    function timelinePositionAt(contentPosition) {
        return Math.max(0, Math.min(timelineController.durationMs,
            (contentPosition - root.trackHeaderWidth) * 1000 / timelineController.pixelsPerSecond))
    }

    function scrubTimelineAt(contentPosition, immediate) {
        root.pendingTimelineScrubMs = root.timelinePositionAt(contentPosition)
        viewerTabs.currentIndex = 1
        if (immediate) {
            timelineScrubTimer.stop()
            timelineController.playheadMs = root.pendingTimelineScrubMs
            root.pendingTimelineScrubMs = -1
        } else if (!timelineScrubTimer.running) {
            timelineScrubTimer.start()
        }
    }

    function beginTimelineScrub(contentPosition) {
        root.timelineScrubbing = true
        root.resumeAfterTimelineScrub = root.programPlaybackRequested
        programPlayer.pause()
        instantProgramPlayer.pause()
        root.instantPlaybackRequested = false
        if (!root.resumeAfterTimelineScrub && timelineController.livePreviewUrl.toString() !== "") {
            programPlayer.stop()
            timelineController.stopLivePreview()
        }
        root.scrubTimelineAt(contentPosition, true)
    }

    function finishTimelineScrub(contentPosition) {
        root.scrubTimelineAt(contentPosition, true)
        root.timelineScrubbing = false
        const shouldResume = root.resumeAfterTimelineScrub && root.programPlaybackRequested
        root.resumeAfterTimelineScrub = false
        if (shouldResume)
            root.startRequestedProgramPlayback()
    }

    function zoomTimelineAt(factor, viewportX) {
        root.suspendTimelineFollow()
        const oldZoom = timelineController.pixelsPerSecond
        const anchorX = Math.max(root.trackHeaderWidth, Math.min(timelineFlick.width, viewportX))
        const anchorTimeMs = Math.max(0, (timelineFlick.contentX + anchorX - root.trackHeaderWidth) * 1000 / oldZoom)
        const newZoom = Math.max(5, Math.min(4000, oldZoom * factor))
        if (Math.abs(newZoom - oldZoom) < 0.001)
            return
        timelineController.pixelsPerSecond = newZoom
        Qt.callLater(function() {
            timelineFlick.contentX = Math.max(0, Math.min(timelineFlick.contentWidth - timelineFlick.width,
                root.trackHeaderWidth + anchorTimeMs * timelineController.pixelsPerSecond / 1000 - anchorX))
        })
    }

    function refreshTimelineContentWidth() {
        root.timelineContentWidth = Math.max(1024,
            root.trackHeaderWidth + Math.max(30000, timelineController.durationMs + 5000) *
            timelineController.pixelsPerSecond / 1000)
    }

    function fitTimelineToWindow() {
        root.suspendTimelineFollow()
        const usableWidth = Math.max(1, timelineFlick.width - root.trackHeaderWidth - 16)
        const seconds = Math.max(1, timelineController.durationMs / 1000)
        timelineController.pixelsPerSecond = Math.max(5, Math.min(4000, usableWidth / seconds))
        timelineFlick.contentX = 0
    }

    function suspendTimelineFollow() {
        root.timelineFollowSuppressed = true
        timelineFollowResume.restart()
    }

    function followTimelinePlayhead() {
        if (!timelineFlick || !timelineHorizontalScrollBar ||
                timelineFlick.width <= root.trackHeaderWidth || timelineMiddlePan.active ||
                timelineHorizontalScrollBar.pressed || root.timelineScrubbing)
            return
        const maximum = Math.max(0, timelineFlick.contentWidth - timelineFlick.width)
        if (maximum <= 0)
            return
        const usable = Math.max(1, timelineFlick.width - root.trackHeaderWidth - 18)
        const playheadContentX = root.trackHeaderWidth + timelineController.playheadMs *
            timelineController.pixelsPerSecond / 1000
        const viewportX = playheadContentX - timelineFlick.contentX
        let target = timelineFlick.contentX
        if (root.programPlaybackRequested && !root.timelineFollowSuppressed) {
            const forwardAnchor = root.trackHeaderWidth + usable * 0.72
            const backwardAnchor = root.trackHeaderWidth + usable * 0.18
            if (viewportX > forwardAnchor)
                target = playheadContentX - forwardAnchor
            else if (viewportX < backwardAnchor)
                target = playheadContentX - backwardAnchor
        } else if (!root.programPlaybackRequested) {
            const leftEdge = root.trackHeaderWidth + usable * 0.05
            const rightEdge = root.trackHeaderWidth + usable * 0.95
            if (viewportX < leftEdge)
                target = playheadContentX - leftEdge
            else if (viewportX > rightEdge)
                target = playheadContentX - rightEdge
        }
        target = Math.max(0, Math.min(maximum, target))
        if (Math.abs(target - timelineFlick.contentX) > 0.5)
            timelineFlick.contentX = target
    }

    function captureSessionState() {
        projectController.updateSessionState({
            "playheadMs": timelineController.playheadMs,
            "pixelsPerSecond": timelineController.pixelsPerSecond,
            "activeSequenceId": timelineController.activeSequenceId,
            "selectedIds": timelineController.selectedIds,
            "currentMediaId": projectController.sessionState.currentMediaId || "",
            "sourcePositionMs": sourcePlayer.position,
            "markInMs": root.markInMs,
            "markOutMs": root.markOutMs,
            "workspaceMode": uiSettings.workspaceMode,
            "viewerIndex": viewerTabs.currentIndex,
            "libraryTab": libraryTabs.currentIndex,
            "inspectorTab": inspectorTabs.currentIndex,
            "editSection": editSection.currentIndex,
            "ytpSection": ytpSection.currentIndex,
            "finishSection": finishSection.currentIndex,
            "remixSection": remixSection.currentIndex,
            "timelineContentX": timelineFlick.contentX,
            "timelineContentY": timelineFlick.contentY,
            "libraryCollapsed": uiSettings.libraryCollapsed,
            "inspectorCollapsed": uiSettings.inspectorCollapsed,
            "inspectorContentIndex": root.inspectorContentIndex
        })
    }

    function runPendingDestructiveAction() {
        const action = root.pendingDestructiveAction
        root.pendingDestructiveAction = ""
        if (action === "new")
            projectController.newProject()
        else if (action === "open")
            openDialog.open()
        else if (action === "close") {
            root.allowClose = true
            root.close()
        }
    }

    function requestDestructiveAction(action) {
        if (projectController.dirty) {
            projectController.flushRecoveryJournal()
            root.pendingDestructiveAction = action
            unsavedChangesDialog.open()
        } else {
            root.pendingDestructiveAction = action
            root.runPendingDestructiveAction()
        }
    }

    function saveBeforePendingAction() {
        root.captureSessionState()
        if (projectController.saveProject()) {
            root.runPendingDestructiveAction()
        } else {
            root.continueAfterSave = true
            saveDialog.open()
        }
    }

    function restoreSessionState() {
        const state = projectController.sessionState
        if (!state)
            return
        uiSettings.workspaceMode = Number(state.workspaceMode === undefined ? uiSettings.workspaceMode : state.workspaceMode)
        viewerTabs.currentIndex = Number(state.viewerIndex === undefined ? 1 : state.viewerIndex)
        libraryTabs.currentIndex = Number(state.libraryTab || 0)
        inspectorTabs.currentIndex = Number(state.inspectorTab || 0)
        uiSettings.libraryCollapsed = Boolean(state.libraryCollapsed || false)
        uiSettings.inspectorCollapsed = Boolean(state.inspectorCollapsed || false)
        root.inspectorContentIndex = Number(state.inspectorContentIndex === undefined ? 1 : state.inspectorContentIndex)
        editSection.currentIndex = Number(state.editSection || 0)
        ytpSection.currentIndex = Number(state.ytpSection || 0)
        finishSection.currentIndex = Number(state.finishSection || 0)
        remixSection.currentIndex = Number(state.remixSection || 0)
        root.markInMs = Number(state.markInMs || 0)
        root.markOutMs = Number(state.markOutMs === undefined ? projectController.sourceDurationMs : state.markOutMs)
        Qt.callLater(function() {
            sourcePlayer.position = Number(state.sourcePositionMs || 0)
            timelineFlick.contentX = Math.max(0, Math.min(timelineFlick.contentWidth - timelineFlick.width, Number(state.timelineContentX || 0)))
            timelineFlick.contentY = Math.max(0, Math.min(timelineFlick.contentHeight - timelineFlick.height, Number(state.timelineContentY || 0)))
        })
    }

    function stepTimelineFrame(direction) {
        root.shuttleRate = 0
        sourcePlayer.pause()
        programPlayer.pause()
        instantProgramPlayer.pause()
        root.programPlaybackRequested = false
        root.instantPlaybackRequested = false
        if (timelineController.livePreviewUrl.toString() !== "") {
            programPlayer.stop()
            timelineController.stopLivePreview()
        }
        viewerTabs.currentIndex = 1
        timelineController.playheadMs = timelineController.stepFrame(timelineController.playheadMs, direction)
    }

    function textEditorHasFocus() {
        return root.activeFocusItem && (root.activeFocusItem instanceof TextInput || root.activeFocusItem instanceof TextEdit)
    }

    function timelineTrackAt(contentY) {
        let top = 34
        for (let index = 0; index < timelineController.tracks.length; ++index) {
            const bottom = top + timelineController.tracks[index].height
            if (contentY < bottom)
                return index
            top = bottom
        }
        return Math.max(0, timelineController.tracks.length - 1)
    }

    function beginTimelineMarquee(area, x, y) {
        const point = area.mapToItem(timelineColumn, x, y)
        root.marqueeStartX = point.x
        root.marqueeStartY = point.y
        root.marqueeCurrentX = point.x
        root.marqueeCurrentY = point.y
        root.marqueeActive = true
    }

    function updateTimelineMarquee(area, x, y) {
        if (!root.marqueeActive)
            return
        const point = area.mapToItem(timelineColumn, x, y)
        root.marqueeCurrentX = Math.max(root.trackHeaderWidth, point.x)
        root.marqueeCurrentY = Math.max(34, Math.min(timelineColumn.height, point.y))
    }

    function finishTimelineMarquee(area, x, y) {
        if (!root.marqueeActive)
            return
        root.updateTimelineMarquee(area, x, y)
        const startMs = Math.max(0, (Math.min(root.marqueeStartX, root.marqueeCurrentX) - root.trackHeaderWidth) * 1000 / timelineController.pixelsPerSecond)
        const endMs = Math.max(0, (Math.max(root.marqueeStartX, root.marqueeCurrentX) - root.trackHeaderWidth) * 1000 / timelineController.pixelsPerSecond)
        timelineController.selectBox(startMs, endMs,
                                     root.timelineTrackAt(Math.min(root.marqueeStartY, root.marqueeCurrentY)),
                                     root.timelineTrackAt(Math.max(root.marqueeStartY, root.marqueeCurrentY)))
        root.marqueeActive = false
    }

    function splitTimelineAtPlayhead() {
        if (timelineController.selectedIds.length === 0)
            timelineController.selectBox(timelineController.playheadMs, timelineController.playheadMs,
                                         0, Math.max(0, timelineController.tracks.length - 1))
        return timelineController.splitSelected()
    }

    function toggleViewerPlayback() {
        if (root.activeViewerIndex !== 1) {
            programPlayer.pause()
            instantProgramPlayer.pause()
            root.programPlaybackRequested = false
            root.instantPlaybackRequested = false
            root.playProgramWhenReady = false
            root.resumeProgramAfterSeek = false
            sourcePlayer.playbackState === NativeMediaPlayer.PlayingState ? sourcePlayer.pause() : sourcePlayer.play()
            return
        }
        root.toggleProgramPlayback()
    }

    function programPreviewContains(positionMs) {
        return timelineController.livePreviewUrl.toString() !== "" &&
            positionMs >= timelineController.livePreviewStartMs && positionMs < timelineController.durationMs
    }

    function seekProgramPlayer(timelinePositionMs) {
        if (timelineController.livePreviewUrl.toString() === "" ||
                timelinePositionMs < timelineController.livePreviewStartMs)
            return false
        const desired = Math.max(0, timelinePositionMs - timelineController.livePreviewStartMs)
        const displayedPosition = programPlayer.playbackState === NativeMediaPlayer.PausedState &&
                timelineController.presentedFrameTimestampUs >= 0
            ? timelineController.presentedFrameTimestampUs / 1000 : programPlayer.position
        const tolerance = programPlayer.playbackState === NativeMediaPlayer.PausedState ? 1 : 80
        if (Math.abs(displayedPosition - desired) <= tolerance) {
            root.pendingProgramPosition = -1
            return true
        }
        if (!programPlayer.seekable)
            return false
        root.pendingProgramPosition = desired
        programPlayer.position = desired
        return true
    }

    function instantPreviewAvailable() {
        const preview = timelineController.instantPreview
        return preview && preview.url && preview.url.toString() !== ""
    }

    function directProgramPlaybackAvailable() {
        if (!root.instantPreviewAvailable())
            return false
        return Boolean(timelineController.instantPreview.exact) ||
            (root.smoothDraftPlayback && Boolean(timelineController.instantPreview.draftSafe))
    }

    function seekInstantProgramPlayer() {
        if (!root.instantPreviewAvailable())
            return false
        const desired = Math.max(0, Number(timelineController.instantPreview.sourcePositionMs || 0))
        if (Math.abs(instantProgramPlayer.position - desired) <= 20) {
            root.pendingInstantPosition = -1
            return true
        }
        root.pendingInstantPosition = desired
        instantProgramPlayer.position = desired
        return true
    }

    function syncDirectProgramPlayback(forceSeek) {
        const preview = timelineController.instantPreview
        const available = preview && preview.url && preview.url.toString() !== ""
        if (!available) {
            instantProgramPlayer.pause()
            instantProgramPlayer.muted = true
            root.instantPlaybackRequested = false
            root.instantPreviewActive = false
            root.pendingInstantPosition = -1
            root.timelineClockItemId = ""
            root.timelineClockPreviewExact = false
            return false
        }

        const itemId = String(preview.itemId || "")
        const sourceUrl = preview.url.toString()
        const desired = Math.max(0, Number(preview.sourcePositionMs || 0))
        const sourceChanged = instantProgramPlayer.source.toString() !== sourceUrl
        const itemChanged = root.timelineClockItemId !== itemId

        root.instantPreviewActive = true
        root.instantPlaybackRequested = true
        root.timelineClockPreviewExact = Boolean(preview.exact)
        instantProgramPlayer.muted = !Boolean(preview.audioEnabled)
        instantProgramPlayer.playbackRate = Math.max(0.01, Number(preview.speed || 1))

        if (sourceChanged) {
            instantProgramPlayer.pause()
            root.pendingInstantPosition = desired
            instantProgramPlayer.source = preview.url
        } else if (forceSeek || itemChanged) {
            if (Math.abs(instantProgramPlayer.position - desired) <= 30) {
                root.pendingInstantPosition = -1
            } else {
                root.pendingInstantPosition = desired
                instantProgramPlayer.position = desired
            }
        }

        root.timelineClockItemId = itemId
        if (root.pendingInstantPosition < 0 && root.programPlaybackRequested &&
                !root.timelineScrubbing && instantProgramPlayer.playbackState !== NativeMediaPlayer.PlayingState)
            instantProgramPlayer.play()
        return true
    }

    function leaveInstantProgramPlayback() {
        instantProgramPlayer.pause()
        root.instantPlaybackRequested = false
        root.instantPreviewActive = false
        root.pendingInstantPosition = -1
    }

    function stopProgramPlayback() {
        root.programPlaybackRequested = false
        root.instantPlaybackRequested = false
        root.playProgramWhenReady = false
        root.resumeProgramAfterSeek = false
        root.resumeAfterTimelineScrub = false
        root.timelineClockLastTimestamp = 0
        root.timelineClockItemId = ""
        root.timelineClockPreviewExact = false
        programPlayer.pause()
        instantProgramPlayer.pause()
        instantProgramPlayer.muted = true
    }

    function startRequestedProgramPlayback() {
        viewerTabs.currentIndex = 1
        sourcePlayer.pause()
        if (!root.programPlaybackRequested || root.timelineScrubbing)
            return
        timelineController.cancelPlaybackPreviewRendering()
        if (timelineController.livePreviewUrl.toString() !== "" || timelineController.livePreviewStarting)
            timelineController.stopLivePreview()
        programPlayer.stop()
        root.playProgramWhenReady = false
        root.resumeProgramAfterSeek = false
        root.timelineClockLastTimestamp = Date.now()
        root.syncDirectProgramPlayback(true)
    }

    function toggleProgramPlayback() {
        if (root.programPlaybackRequested) {
            root.stopProgramPlayback()
            return
        }
        root.programPlaybackRequested = true
        root.startRequestedProgramPlayback()
    }

    function editClip(id, name, tags, notes, color, bin, favorite, inMs, outMs) {
        editingClipId = id
        editName.text = name
        editTags.text = tags.join(", ")
        editNotes.text = notes
        editColor.text = color
        editBin.text = bin
        editFavorite.checked = favorite
        editIn.value = inMs
        editOut.value = outMs
        clipEditor.open()
    }

    function renameTimelineClip(itemId, currentName) {
        root.editingTimelineItemId = itemId
        timelineClipName.text = currentName
        timelineClipRenameDialog.open()
        timelineClipName.forceActiveFocus()
        timelineClipName.selectAll()
    }

    function formatTime(milliseconds) {
        const totalSeconds = Math.max(0, Math.floor(milliseconds / 1000))
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds % 3600) / 60)
        const seconds = totalSeconds % 60
        const millis = Math.floor(milliseconds % 1000)
        return String(hours).padStart(2, "0") + ":" +
               String(minutes).padStart(2, "0") + ":" +
               String(seconds).padStart(2, "0") + "." +
               String(millis).padStart(3, "0")
    }

    function readableClipName(name) {
        let clean = String(name || "Clip")
        clean = clean.replace(/\.[^.]+$/, "")
        clean = clean.replace(/^YTDown(?:\.com)?[_ -]*/i, "")
        clean = clean.replace(/^(?:YouTube|Downloaded Video)[_ -]*/i, "")
        clean = clean.replace(/[_-]+/g, " ")
        clean = clean.replace(/\s+(?:Be\s+)?Media\s+[A-Za-z0-9]{8,}(?:\s+\d+)*(?:\s+\d{3,4}p)?$/i, "")
        clean = clean.replace(/\s+(?:[A-Za-z0-9]{10,}|\d{3,4}p)$/i, "")
        clean = clean.replace(/\s+/g, " ").trim()
        return clean || "Clip"
    }

    function effectBrowserEntries(searchText, filterName) {
        const query = String(searchText || "").trim().toLowerCase()
        const activeFilter = String(filterName || "All")
        const entries = []
        const effects = timelineController.availableEffects || []
        for (let index = 0; index < effects.length; ++index) {
            const effect = effects[index]
            const filterMatch = activeFilter === "All" ||
                (activeFilter === "Video" && !effect.audio) ||
                (activeFilter === "Audio" && effect.audio) ||
                (activeFilter === "YTP" && effect.ytp) ||
                (activeFilter === "Favorites" && effect.favorite)
            const haystack = (String(effect.name) + " " + String(effect.category) + " " +
                String(effect.description) + " " + String(effect.tags)).toLowerCase()
            if (filterMatch && (query === "" || haystack.includes(query)))
                entries.push(effect)
        }
        const presets = timelineController.effectPresets || []
        for (let index = 0; index < presets.length; ++index) {
            const name = String(presets[index])
            if (activeFilter === "All" && (query === "" || name.toLowerCase().includes(query)))
                entries.push({"name": name, "type": "", "category": "Presets",
                              "audio": false, "description": "A saved stack of effect settings.",
                              "favorite": false, "ytp": false, "heavy": false, "preset": true})
        }
        entries.sort(function(left, right) {
            const rank = function(category) {
                const value = String(category)
                if (value === "Presets") return 0
                if (value === "Video / Glitch & Signal") return 1
                if (value === "Video / Time & Trails") return 2
                if (value.startsWith("Video")) return 3
                if (value === "Audio / Destruction") return 4
                return value.startsWith("Audio") ? 5 : 6
            }
            const categoryOrder = rank(left.category) - rank(right.category) ||
                String(left.category).localeCompare(String(right.category))
            return categoryOrder !== 0 ? categoryOrder : String(left.name).localeCompare(String(right.name))
        })
        return entries
    }

    function applyEffectBrowserEntry(entry) {
        if (!entry || timelineController.selectedIds.length === 0)
            return false
        const applied = entry.preset
            ? timelineController.applyEffectPreset(entry.name)
            : timelineController.addEffectToSelection(entry.type)
        if (applied)
            toastOverlay.show((entry.preset ? "Applied preset: " : "Added effect: ") + entry.name)
        return applied
    }

    function executePaletteCommand(command) {
        if (command === "import") importDialog.open()
        else if (command === "save") { root.captureSessionState(); if (!projectController.saveProject()) saveDialog.open() }
        else if (command === "export") exportDialog.open()
        else if (command === "split") root.splitTimelineAtPlayhead()
        else if (command === "marker") timelineController.addMarker(timelineController.playheadMs, "Marker")
        else if (command === "fit") root.fitTimelineToWindow()
        else if (command === "reverse") timelineController.setReverse(!(timelineController.inspector.reverse || false))
        else if (command === "stutter") timelineController.buildStutter(4, 80, true)
        else if (command === "shake") timelineController.addEffect(0, timelineController.inspector.itemId, "screen_shake")
        else if (command === "source") viewerTabs.currentIndex = 0
        else if (command === "program") viewerTabs.currentIndex = 1
        else if (command === "library") uiSettings.libraryCollapsed = !uiSettings.libraryCollapsed
        else if (command === "inspector") uiSettings.inspectorCollapsed = !uiSettings.inspectorCollapsed
    }

    QtObject {
        id: theme
        readonly property color canvas: "#080a0f"
        readonly property color surfaceSunken: "#0c0f16"
        readonly property color panel: "#11151d"
        readonly property color panelRaised: "#181d27"
        readonly property color panelHigh: "#202633"
        readonly property color panelHover: "#272e3d"
        readonly property color border: "#303847"
        readonly property color borderStrong: "#49556a"
        readonly property color text: "#f4f6fb"
        readonly property color muted: "#a4adbd"
        readonly property color faint: "#697487"
        readonly property color accent: "#9b8cff"
        readonly property color accentBright: "#b9afff"
        readonly property color accentDark: "#6556cf"
        readonly property color cyan: "#53d7ff"
        readonly property color green: "#62d9a5"
        readonly property color amber: "#f3bd68"
        readonly property color danger: "#ff7185"
        readonly property color shadow: "#66000000"
        readonly property color bgSunken: surfaceSunken
        readonly property color bgRaised: panelRaised
        readonly property color bgHover: panelHover
        readonly property color bgActive: panelHigh
    }

    palette.window: theme.panel
    palette.windowText: theme.text
    palette.base: theme.canvas
    palette.alternateBase: theme.panelRaised
    palette.text: theme.text
    palette.placeholderText: theme.faint
    palette.button: theme.panelRaised
    palette.buttonText: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: "#ffffff"
    palette.light: theme.borderStrong
    palette.mid: theme.border
    palette.dark: theme.canvas

    component Panel: Rectangle {
        color: theme.panel
        border.color: theme.border
        border.width: 0
        radius: 0
    }

    component SectionTitle: Label {
        font.pixelSize: 11
        font.weight: Font.Bold
        font.letterSpacing: .8
        color: theme.muted
        leftPadding: 12
        verticalAlignment: Text.AlignVCenter
    }

    component AppToolButton: ToolButton {
        id: appTool
        property string helpText: ""
        implicitHeight: 32
        implicitWidth: Math.max(30, contentItem.implicitWidth + 14)
        font.pixelSize: 12
        contentItem: Label { text: appTool.text; color: appTool.enabled ? (appTool.checked ? theme.text : theme.muted) : theme.faint; font: appTool.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle {
            radius: 4
            color: appTool.down ? theme.accentDark : appTool.checked ? "#403971" : appTool.hovered ? theme.panelHover : "transparent"
            border.width: appTool.activeFocus || appTool.checked ? 1 : 0
            border.color: appTool.activeFocus ? theme.cyan : theme.accentBright
        }
        ToolTip.visible: appTool.hovered && appTool.helpText !== ""
        ToolTip.delay: 500
        ToolTip.text: appTool.helpText
    }

    component AccentButton: Button {
        id: accentButton
        property string helpText: ""
        implicitHeight: 34
        font.weight: Font.DemiBold
        contentItem: Label { text: accentButton.text; color: accentButton.enabled ? "white" : "#7f6572"; font: accentButton.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle {
            radius: 5
            border.color: accentButton.activeFocus ? theme.cyan : (accentButton.enabled ? theme.accentBright : theme.border)
            border.width: accentButton.activeFocus ? 2 : 1
            gradient: Gradient {
                GradientStop { position: 0; color: accentButton.enabled ? (accentButton.hovered ? theme.accentBright : theme.accent) : theme.panelHigh }
                GradientStop { position: 1; color: accentButton.enabled ? (accentButton.down ? theme.accentDark : "#7567dc") : theme.panelRaised }
            }
        }
        ToolTip.visible: accentButton.hovered && accentButton.helpText !== ""
        ToolTip.delay: 500
        ToolTip.text: accentButton.helpText
    }

    component SectionPicker: ComboBox {
        id: sectionPicker
        Layout.fillWidth: true
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        Layout.preferredHeight: 30
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: "Choose a section directly instead of scrolling through every tool"
    }

    component WorkspaceTab: TabButton {
        id: workspaceTab
        property string helpText: ""
        implicitHeight: 30
        font.pixelSize: 11
        font.weight: checked ? Font.DemiBold : Font.Medium
        contentItem: Label { text: workspaceTab.text; color: workspaceTab.checked ? theme.text : theme.muted; font: workspaceTab.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle {
            color: workspaceTab.checked ? theme.panelHigh : workspaceTab.hovered ? theme.panelHover : "transparent"
            Rectangle { anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;height:1;color:workspaceTab.checked?"#70ffffff":"transparent" }
            Rectangle { anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:2;color:theme.accent;visible:workspaceTab.checked }
        }
        ToolTip.visible: workspaceTab.hovered && workspaceTab.helpText !== ""
        ToolTip.delay: 500
        ToolTip.text: workspaceTab.helpText
    }

    component Pill: Label {
        leftPadding: 8; rightPadding: 8; topPadding: 3; bottomPadding: 3
        font.pixelSize: 10; font.weight: Font.DemiBold
        background: Rectangle { radius: 3; color: theme.panelHigh; border.color: theme.borderStrong }
    }

    component InspectorCard: GroupBox {
        id: inspectorCard
        Layout.fillWidth: true
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        padding: 8
        topPadding: 32
        bottomPadding: 8
        background: Rectangle {
            radius: 0; border.color: theme.border; border.width: 0; opacity: inspectorCard.enabled ? 1 : .65
            color: "transparent"
        }
        label: Item {
            x: 8; y: 0; width: inspectorCard.width-16; height: 28
            Label { anchors.left:parent.left;anchors.verticalCenter:parent.verticalCenter;text:inspectorCard.title;font.pixelSize:11;font.weight:Font.DemiBold;color:inspectorCard.enabled?theme.text:theme.faint }
            Rectangle { anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:1;color:theme.border }
        }
    }

    component WorkspaceHeader: Rectangle {
        id: workspaceHeader
        property string heading: "WORKSPACE"
        property string description: ""
        property color accentColor: theme.accent
        Layout.fillWidth: true
        Layout.preferredHeight: description ? 38 : 30
        color: theme.panel
        Rectangle { width:2;height:parent.height-12;anchors.left:parent.left;anchors.leftMargin:6;anchors.verticalCenter:parent.verticalCenter;radius:1;color:workspaceHeader.accentColor }
        Column { anchors.left:parent.left;anchors.leftMargin:14;anchors.right:parent.right;anchors.rightMargin:8;anchors.verticalCenter:parent.verticalCenter;spacing:0
            Label { width:parent.width;text:workspaceHeader.heading;font.pixelSize:12;font.weight:Font.DemiBold;color:theme.text;elide:Text.ElideRight }
            Label { width:parent.width;visible:workspaceHeader.description!=="";text:workspaceHeader.description;font.pixelSize:9;color:theme.muted;elide:Text.ElideRight }
        }
        Rectangle { anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:1;color:theme.border }
    }

    FileDialog {
        id: importDialog
        title: "Import media — the first source starts the timeline"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Media files (*.mp4 *.mov *.mkv *.avi *.webm *.mp3 *.wav *.flac *.m4a)", "All files (*)"]
        onAccepted: projectController.importMedia(selectedFile)
    }
    FileDialog { id: whisperModelDialog; title:"Choose a local whisper.cpp model";fileMode:FileDialog.OpenFile;nameFilters:["Whisper models (*.bin)"];onAccepted:projectController.transcribeCurrentMedia(selectedFile,transcriptLanguage.currentValue) }
    FileDialog { id: archiveDialog;title:"Collect project archive";fileMode:FileDialog.SaveFile;defaultSuffix:"zip";nameFilters:["ZIP archives (*.zip)"];onAccepted:projectController.archiveProject(selectedFile) }
    Dialog {
        id: saveEffectPresetDialog
        title: "Save Effect Preset"
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: effectPresetName.forceActiveFocus()
        onAccepted: {
            if (timelineController.saveEffectPreset(effectPresetName.text))
                effectPresetName.clear()
        }
        ColumnLayout {
            width: 300
            Label { text: "Name"; color: theme.muted }
            TextField { id: effectPresetName; Layout.fillWidth: true; placeholderText: "Preset name" }
        }
    }
    FileDialog {
        id: relinkDialog
        title: "Relink " + projectController.missingMediaName
        fileMode: FileDialog.OpenFile
        nameFilters: ["Media files (*.mp4 *.mov *.mkv *.avi *.webm *.mp3 *.wav *.flac *.m4a)", "All files (*)"]
        onAccepted: projectController.relinkMissingMedia(selectedFile)
    }

    Dialog {
        id: recoveryDialog
        title: "Restore the latest recovery session?"
        modal: true
        anchors.centerIn: parent
        visible: projectController.recoveryAvailable
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: "A newer compact recovery session was found. Restore it, or keep the manual-save baseline?"; wrapMode: Text.Wrap }
        onAccepted: projectController.recoverAutosave()
        onRejected: projectController.discardRecovery()
    }
    Dialog {
        id: unsavedChangesDialog
        objectName: "unsavedChangesDialog"
        title: "Unsaved changes"
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        ColumnLayout {
            spacing: 14
            Label { text: "Save your changes before continuing?"; color: theme.text }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "Cancel"; onClicked: { root.pendingDestructiveAction=""; unsavedChangesDialog.close() } }
                Button { text: "Discard"; onClicked: { if(root.pendingDestructiveAction==="close")projectController.discardJournal(); unsavedChangesDialog.close(); root.runPendingDestructiveAction() } }
                Button { text: "Save"; highlighted: true; onClicked: { unsavedChangesDialog.close(); root.saveBeforePendingAction() } }
            }
        }
    }

    Dialog {
        id: clipEditor
        title: "Edit reusable clip"
        modal: true
        anchors.centerIn: parent
        width: 480
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: projectController.updateLibraryClip(
            root.editingClipId, editName.text, editTags.text, editNotes.text,
            editColor.text, editBin.text, editFavorite.checked,
            editIn.value, editOut.value)
        ColumnLayout {
            anchors.fill: parent
            Label { text: "Name" }
            TextField { id: editName; Layout.fillWidth: true }
            Label { text: "Tags (comma separated)" }
            TextField { id: editTags; Layout.fillWidth: true }
            Label { text: "Notes" }
            TextArea { id: editNotes; Layout.fillWidth: true; Layout.preferredHeight: 70; wrapMode: TextEdit.Wrap }
            RowLayout {
                Label { text: "Bin" }
                TextField { id: editBin; Layout.fillWidth: true }
                Label { text: "Color" }
                TextField { id: editColor; Layout.preferredWidth: 100 }
            }
            CheckBox { id: editFavorite; text: "Favorite" }
            RowLayout {
                Label { text: "In (ms)" }
                SpinBox { id: editIn; from: 0; to: 2147483647; editable: true; Layout.fillWidth: true }
                Label { text: "Out (ms)" }
                SpinBox { id: editOut; from: 1; to: 2147483647; editable: true; Layout.fillWidth: true }
            }
            Button {
                text: "Delete clip"
                palette.button: "#713044"
                onClicked: {
                    projectController.deleteLibraryClip(root.editingClipId)
                    clipEditor.reject()
                }
            }
        }
    }
    Dialog {
        id: timelineClipRenameDialog
        objectName: "timelineClipRenameDialog"
        title: "Rename timeline clip"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: timelineController.renameItem(root.editingTimelineItemId, timelineClipName.text)
        TextField {
            id: timelineClipName
            objectName: "timelineClipNameField"
            width: 360
            placeholderText: "Clip name"
            onAccepted: timelineClipRenameDialog.accept()
        }
    }
    Dialog {
        id: shortcutEditor
        title: "Timeline keyboard shortcuts"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: {
            splitKey.text = timelineController.shortcut("split"); deleteKey.text = timelineController.shortcut("delete")
            duplicateKey.text = timelineController.shortcut("duplicate"); copyKey.text = timelineController.shortcut("copy")
            pasteKey.text = timelineController.shortcut("paste"); groupKey.text = timelineController.shortcut("group")
            unlinkKey.text = timelineController.shortcut("unlink")
        }
        onAccepted: timelineController.configureShortcuts({"split":splitKey.text,"delete":deleteKey.text,
            "duplicate":duplicateKey.text,"copy":copyKey.text,"paste":pasteKey.text,"group":groupKey.text,"unlink":unlinkKey.text})
        GridLayout {
            columns: 2
            Label { text: "Split" } TextField { id: splitKey }
            Label { text: "Delete" } TextField { id: deleteKey }
            Label { text: "Duplicate" } TextField { id: duplicateKey }
            Label { text: "Copy" } TextField { id: copyKey }
            Label { text: "Paste" } TextField { id: pasteKey }
            Label { text: "Group" } TextField { id: groupKey }
            Label { text: "Unlink" } TextField { id: unlinkKey }
            Button { text: "VEGAS-style defaults"; Layout.columnSpan: 2; onClicked: {
                timelineController.useVegasShortcuts(); splitKey.text="S"; deleteKey.text="Delete"; duplicateKey.text="D";
                copyKey.text="Ctrl+C"; pasteKey.text="Ctrl+V"; groupKey.text="G"; unlinkKey.text="U"
            } }
        }
    }
    FileDialog {
        id: saveDialog
        title: "Save exact YTP Editor session"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ytps"
        nameFilters: ["YTP Editor sessions (*.ytps)"]
        onAccepted: {
            root.captureSessionState()
            const saved = projectController.saveProject(selectedFile)
            if (saved && root.continueAfterSave) {
                root.continueAfterSave = false
                root.runPendingDestructiveAction()
            }
        }
        onRejected: { root.continueAfterSave = false; root.pendingDestructiveAction = "" }
    }
    FileDialog {
        id: openDialog
        title: "Restore YTP Editor session or legacy project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["YTP Editor sessions (*.ytps)", "Legacy YTP projects (*.ytp.json)"]
        onAccepted: projectController.openProject(selectedFile)
    }
    FileDialog {
        id: exportFileDialog
        title: "Choose export file"
        fileMode: FileDialog.SaveFile
        defaultSuffix: exportPreset.currentIndex >= 0 ? exportController.presets[exportPreset.currentIndex].container : "mp4"
        nameFilters: ["Video or audio (*.*)"]
        onAccepted: root.exportPath = selectedFile.toString()
    }
    FileDialog {
        id: snapshotDialog
        title: "Save program snapshot"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["PNG image (*.png)"]
        onAccepted: exportController.saveSnapshot(selectedFile, timelineController.playheadMs)
    }

    Dialog {
        id: exportDialog
        title: "Export and Render Queue"
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 80, 820)
        height: Math.min(root.height - 80, 720)
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            GroupBox {
                title: "New render"
                Layout.fillWidth: true
                GridLayout {
                    anchors.fill: parent; columns: 4
                    Label { text: "Preset" }
                    ComboBox {
                        id: exportPreset; Layout.columnSpan: 3; Layout.fillWidth: true
                        model: exportController.presets; textRole: "name"
                        valueRole: "id"
                        onActivated: {
                            const p = exportController.presets[currentIndex]
                            customWidth.value = Math.max(16, p.width || 1920)
                            customHeight.value = Math.max(16, p.height || 1080)
                            customVideoRate.value = Math.max(100, p.videoBitrateKbps || 12000)
                            customAudioRate.value = Math.max(32, p.audioBitrateKbps || 320)
                        }
                    }
                    Label { text: "Output" }
                    TextField { Layout.columnSpan: 2; Layout.fillWidth: true; readOnly: true; text: root.exportPath; placeholderText: "Choose a destination…" }
                    Button { text: "Browse…"; onClicked: exportFileDialog.open() }
                    CheckBox { id: markedExport; text: "Render marked region"; Layout.columnSpan: 4 }
                    Label { text: "Start (ms)" }
                    SpinBox { id: exportStart; from: 0; to: 2147483647; value: 0; editable: true; enabled: markedExport.checked }
                    Label { text: "End (ms)" }
                    SpinBox { id: exportEnd; from: 1; to: 2147483647; value: Math.max(1, timelineController.durationMs); editable: true; enabled: markedExport.checked }
                    Label { text: "Custom width" }
                    SpinBox { id: customWidth; from: 16; to: 7680; value: 1920; editable: true }
                    Label { text: "Custom height" }
                    SpinBox { id: customHeight; from: 16; to: 4320; value: 1080; editable: true }
                    Label { text: "Video kb/s" }
                    SpinBox { id: customVideoRate; from: 100; to: 200000; value: 12000; editable: true }
                    Label { text: "Audio kb/s" }
                    SpinBox { id: customAudioRate; from: 32; to: 1536; value: 320; editable: true }
                    RowLayout {
                        Layout.columnSpan: 4; Layout.fillWidth: true
                        Button { text: "Save settings as preset"; onClicked: exportController.saveCustomPreset("Custom " + customWidth.value + "x" + customHeight.value,
                            {"container":"mp4","width":customWidth.value,"height":customHeight.value,"videoCodec":"libx264","audioCodec":"aac","videoBitrateKbps":customVideoRate.value,"audioBitrateKbps":customAudioRate.value,"audioSampleRate":48000}) }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Add to Render Queue"; highlighted: true; enabled: root.exportPath !== ""
                            onClicked: exportController.enqueue(root.exportPath, exportPreset.currentValue,
                                exportStart.value, exportEnd.value, markedExport.checked,
                                {"container": exportPreset.currentIndex >= 0 ? exportController.presets[exportPreset.currentIndex].container : "mp4",
                                 "width":customWidth.value,"height":customHeight.value,
                                 "videoCodec": exportPreset.currentIndex >= 0 ? exportController.presets[exportPreset.currentIndex].videoCodec : "libx264",
                                 "audioCodec": exportPreset.currentIndex >= 0 ? exportController.presets[exportPreset.currentIndex].audioCodec : "aac",
                                 "videoBitrateKbps":customVideoRate.value,"audioBitrateKbps":customAudioRate.value,
                                 "audioSampleRate":48000,
                                 "audioOnly": exportPreset.currentIndex >= 0 ? exportController.presets[exportPreset.currentIndex].audioOnly : false})
                        }
                    }
                }
            }
            RowLayout { Layout.fillWidth: true
                Label { text: "RENDER QUEUE"; font.bold: true; Layout.fillWidth: true }
                Button { text: "Clear finished"; onClicked: exportController.clearFinished() }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: exportController.jobs
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width; height: 86; color: "#20252d"; border.color: "#3a424f"; radius: 4
                    ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 3
                        RowLayout { Layout.fillWidth: true
                            Label { text: modelData.fileName; font.bold: true; Layout.fillWidth: true; elide: Text.ElideMiddle }
                            Label { text: modelData.state; color: modelData.state === "Complete" ? "#7fd6a6" : modelData.state === "Failed" ? "#ff7b8d" : "#ffca70" }
                            Button { text: "Cancel"; visible: !modelData.finished; onClicked: exportController.cancel(modelData.jobId) }
                        }
                        ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: modelData.progress }
                        Label { text: modelData.preset + " — " + modelData.message; Layout.fillWidth: true; elide: Text.ElideRight; color: "#aeb5c2" }
                    }
                }
            }
            Label { text: exportController.statusMessage; color: "#aeb5c2"; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }

    Dialog {
        id: tutorialDialog
        title: "Welcome to YTP Editor 0.5"
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width-60,680);height:Math.min(root.height-60,450)
        visible: projectController.firstRunTutorialVisible
        closePolicy: Popup.NoAutoClose
        background:Rectangle{color:theme.panel;radius:12;border.color:theme.borderStrong;border.width:1}
        ColumnLayout {
            anchors.fill: parent;spacing:12
            RowLayout { Layout.fillWidth:true
                Rectangle { width:46;height:46;radius:12;color:theme.accent;Label{anchors.centerIn:parent;text:"Y";font.pixelSize:24;font.weight:Font.Black;color:"white"} }
                ColumnLayout { spacing:0
                    Label{text:["IMPORT & COLLECT","BUILD THE CUT","MAKE IT WEIRD","REMIX FASTER","PREVIEW & EXPORT"][root.tutorialPage];font.pixelSize:20;font.weight:Font.Black;color:theme.text}
                    Label{text:(root.tutorialPage+1)+" OF 5";font.pixelSize:10;font.letterSpacing:1.3;color:theme.accent}
                }
                Item{Layout.fillWidth:true}
            }
            Label {
                Layout.fillWidth:true;Layout.fillHeight:true;wrapMode:Text.Wrap;verticalAlignment:Text.AlignVCenter;color:theme.muted;font.pixelSize:15;lineHeight:1.35
                text:[
                    "Import an infomercial, mark In and Out with I/O, then press C to save that fragment in your reusable Clip Library. Drag it in as many times as you like.",
                    "Drop clips on video or audio tracks. Split with S, duplicate with D, and use VEGAS-style ripple modes so later events stay synchronized.",
                    "Select an event and use Edit for reverse, speed, pitch, transforms, effects, captions and keyframes. Mixer controls remain built into the edit window.",
                    "Use YTP for instant stutters, reverses, repeats and distortion stacks. Remix adds phonetic Sentence Mixer, compounds, beat tools, tracking and visual macros.",
                    "Use Source, Program or Dual monitors while cutting. Continuous Program caches keep complex effects playable, while Export always reads full-resolution originals."
                ][root.tutorialPage]
            }
            RowLayout { Layout.fillWidth:true;spacing:6;Repeater{model:5;Rectangle{required property int index;Layout.fillWidth:true;height:4;radius:2;color:index<=root.tutorialPage?theme.accent:theme.border}} }
            RowLayout { Layout.fillWidth:true
                AppToolButton{text:"Back";enabled:root.tutorialPage>0;onClicked:root.tutorialPage--}
                Item{Layout.fillWidth:true}
                AccentButton{text:root.tutorialPage===4?"Start editing":"Next";Layout.preferredWidth:140;onClicked:{if(root.tutorialPage===4){projectController.dismissFirstRunTutorial();root.tutorialPage=0}else root.tutorialPage++}}
            }
        }
    }

    NativeMediaPlayer {
        id: sourcePlayer
        objectName: "sourcePlayer"
        source: projectController.sourceUrl
        onErrorOccurred: projectController.reportPlaybackError(errorString)
    }
    NativeMediaPlayer {
        id: instantProgramPlayer
        objectName: "instantProgramPlayer"
        source: ""
        playbackRate: 1
        muted: true
        onPositionChanged: {
            if (root.pendingInstantPosition >= 0) {
                if (Math.abs(instantProgramPlayer.position - root.pendingInstantPosition) <= 60) {
                    root.pendingInstantPosition = -1
                    if (root.programPlaybackRequested && root.instantPlaybackRequested &&
                            !root.timelineScrubbing && instantProgramPlayer.playbackState !== NativeMediaPlayer.PlayingState)
                        instantProgramPlayer.play()
                }
            }
        }
        onMediaStatusChanged: {
            if (mediaStatus === NativeMediaPlayer.LoadedMedia || mediaStatus === NativeMediaPlayer.BufferedMedia ||
                    mediaStatus === NativeMediaPlayer.BufferingMedia) {
                root.seekInstantProgramPlayer()
                if (root.programPlaybackRequested && root.instantPlaybackRequested &&
                        !root.timelineScrubbing && root.pendingInstantPosition < 0)
                    play()
            }
        }
        onErrorOccurred: {
            root.instantPlaybackRequested = false
            root.instantPreviewActive = false
            instantProgramPlayer.muted = true
            projectController.reportPlaybackError(errorString)
        }
    }
    NativeMediaPlayer {
        id: programPlayer
        objectName: "programPlayer"
        onErrorOccurred: {
            // Windows can deliver a late network error from the source that was
            // just retired during a seek. The controller owns the current FFmpeg
            // process and will clear livePreviewUrl if that process truly fails.
            if (root.programPlaybackRequested &&
                    (timelineController.livePreviewStarting ||
                     timelineController.livePreviewUrl.toString() !== ""))
                return
            root.playProgramWhenReady = false
            if (!root.programPlaybackRequested)
                projectController.reportPlaybackError(errorString)
        }
        onPositionChanged: {}
        onMediaStatusChanged: {
            if (mediaStatus === NativeMediaPlayer.LoadedMedia || mediaStatus === NativeMediaPlayer.BufferedMedia ||
                    mediaStatus === NativeMediaPlayer.BufferingMedia) {
                if (root.programPlaybackRequested && root.playProgramWhenReady && !root.timelineScrubbing) {
                    root.playProgramWhenReady = false
                    root.resumeProgramAfterSeek = false
                    play()
                }
            } else if (mediaStatus === NativeMediaPlayer.InvalidMedia) {
                if (!root.programPlaybackRequested)
                    root.playProgramWhenReady = false
            }
        }
        onPlaybackStateChanged: {
            if (playbackState === NativeMediaPlayer.StoppedState) {
                root.lastProgramFrameTimestampUs = -1
            }
        }
    }
    Timer {
        id: timelinePlaybackClock
        interval: 16
        repeat: true
        running: root.programPlaybackRequested && !root.timelineScrubbing
        onRunningChanged: {
            if (running)
                root.timelineClockLastTimestamp = Date.now()
        }
        onTriggered: {
            const now = Date.now()
            if (root.timelineClockLastTimestamp <= 0) {
                root.timelineClockLastTimestamp = now
                return
            }
            // Never catch up a stalled UI by jumping the timeline. A single tick
            // may advance at most 50 ms, eliminating runaway/5x playhead motion.
            const elapsed = Math.max(0, Math.min(50, now - root.timelineClockLastTimestamp))
            root.timelineClockLastTimestamp = now
            const next = Math.min(timelineController.durationMs,
                                  timelineController.playheadMs + elapsed)
            root.timelineClockUpdatingPlayhead = true
            timelineController.setPlaybackPlayheadMs(next)
            root.timelineClockUpdatingPlayhead = false
            if (next >= timelineController.durationMs) {
                root.stopProgramPlayback()
                return
            }
            root.syncDirectProgramPlayback(false)
        }
    }
    Timer {
        id: timelineFollowResume
        interval: 1400
        repeat: false
        onTriggered: {
            root.timelineFollowSuppressed = false
            if (root.programPlaybackRequested)
                root.followTimelinePlayhead()
        }
    }
    Timer {
        id: timelineScrubTimer
        interval: 16
        repeat: false
        onTriggered: {
            if (root.pendingTimelineScrubMs < 0)
                return
            const requestedMs = root.pendingTimelineScrubMs
            root.pendingTimelineScrubMs = -1
            timelineController.playheadMs = requestedMs
        }
    }
    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: root.captureSessionState()
    }
    Timer {
        id: instantPreviewRelease
        interval: 180
        repeat: false
        onTriggered: {
            if (root.timelineScrubbing || root.instantPlaybackRequested)
                return
            if (root.programPreviewContains(timelineController.playheadMs)) {
                if (root.seekProgramPlayer(timelineController.playheadMs))
                    root.instantPreviewActive = false
            }
        }
    }
    Timer {
        id: programSeekDebounce
        interval: 180
        repeat: false
        onTriggered: {
            if (root.programPlaybackRequested || root.timelineScrubbing ||
                    (root.instantPreviewAvailable() && timelineController.instantPreview.exact))
                return
            if (timelineController.playheadMs >= timelineController.durationMs ||
                    root.programPreviewContains(timelineController.playheadMs))
                return
            root.playProgramWhenReady = root.resumeProgramAfterSeek
            root.resumeProgramAfterSeek = false
            if (!timelineController.renderPlaybackPreview())
                root.playProgramWhenReady = false
        }
    }
    Connections {
        target: timelineController
        function onTimelineChanged() { root.refreshTimelineContentWidth() }
        function onZoomChanged() { root.refreshTimelineContentWidth() }
        function onTimelineCompacted(removedBeforePlayheadMs) {
            const removedPixels = removedBeforePlayheadMs * timelineController.pixelsPerSecond / 1000
            timelineFlick.contentX = Math.max(0, timelineFlick.contentX - removedPixels)
        }
        function onPlayheadChanged() {
            root.followTimelinePlayhead()
            if (root.timelineClockUpdatingPlayhead || root.instantPlayerUpdatingPlayhead ||
                    root.programPlayerUpdatingPlayhead)
                return
            if (root.programPlaybackRequested) {
                if (root.timelineScrubbing)
                    return
                root.timelineClockLastTimestamp = Date.now()
                root.syncDirectProgramPlayback(true)
                return
            }
            if (!root.programPlaybackRequested && root.instantPreviewAvailable() &&
                    timelineController.instantPreview.exact) {
                root.syncDirectProgramPlayback(true)
                root.instantPlaybackRequested = false
                instantProgramPlayer.muted = true
                instantProgramPlayer.pause()
                instantPreviewRelease.restart()
            } else if (!root.programPlaybackRequested && root.activeViewerIndex === 1) {
                programSeekDebounce.restart()
            }
        }
        function onPlaybackPreviewChanged() {
            if (timelineController.previewRendering)
                return
            const previewUrl = timelineController.playbackPreviewUrl.toString()
            if (previewUrl !== root.countedPreviewUrl) {
                root.countedPreviewUrl = previewUrl
                root.programFrames = 0
                root.programDroppedFrames = 0
                root.lastProgramFrameTimestampUs = -1
            }
            if (timelineController.playbackPreviewUrl.toString() !== "") {
                if (!root.programPreviewContains(timelineController.playheadMs))
                    programSeekDebounce.restart()
            } else if (!timelineController.previewRendering &&
                    !timelineController.livePreviewStarting &&
                    timelineController.livePreviewUrl.toString() === "") {
                root.playProgramWhenReady = false
            }
        }
        function onLivePreviewChanged() {
            if (timelineController.livePreviewStarting)
                root.lastProgramFrameTimestampUs = -1
        }
    }
    Connections {
        target: projectController
        function onSessionRestoreRequested() { Qt.callLater(root.restoreSessionState) }
        function onStatusMessageChanged() { toastOverlay.show(projectController.statusMessage) }
    }
    Connections {
        target: timelineController
        function onPresentedFrameChanged() {
            const timestamp = timelineController.presentedFrameTimestampUs
            if (timestamp < 0) {
                root.lastProgramFrameTimestampUs = -1
                return
            }
            const expectedUs = timelineController.projectFrameDurationMs * 1000
            if (programPlayer.playbackState === NativeMediaPlayer.PlayingState && root.lastProgramFrameTimestampUs >= 0) {
                const delta = timestamp - root.lastProgramFrameTimestampUs
                if (delta > expectedUs * 1.5 && delta < expectedUs * 8)
                    root.programDroppedFrames += Math.max(0, Math.round(delta / expectedUs) - 1)
            }
            root.lastProgramFrameTimestampUs = timestamp
            root.programFrames++
        }
    }
    Timer {
        interval: 33
        repeat: true
        running: root.shuttleRate < 0
        onTriggered: sourcePlayer.position = Math.max(0, sourcePlayer.position + root.shuttleRate * interval)
    }

    Connections {
        target: projectController
        function onSourceChanged() {
            root.markInMs = 0
            root.markOutMs = projectController.sourceDurationMs
        }
        function onSeekSourceRequested(positionMs) {
            sourcePlayer.position = positionMs
            root.markInMs = positionMs
        }
        function onSeekSourceRangeRequested(inMs, outMs) {
            sourcePlayer.position = inMs
            root.markInMs = inMs
            root.markOutMs = outMs
        }
    }

    Shortcut { sequence: "Ctrl+Z"; onActivated: projectController.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: projectController.redo() }
    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        onActivated: {
            root.captureSessionState()
            if (!projectController.saveProject())
                saveDialog.open()
        }
    }
    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: commandPalette.openWithQuery("") }
    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; onActivated: root.requestDestructiveAction("new") }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: root.requestDestructiveAction("open") }
    Shortcut { sequence: "Ctrl+I"; context: Qt.ApplicationShortcut; onActivated: importDialog.open() }
    Shortcut { sequence: timelineController.shortcut("delete"); enabled:!root.textEditorHasFocus(); onActivated: timelineController.deleteSelected() }
    Shortcut { sequence: timelineController.shortcut("split"); enabled:!root.textEditorHasFocus(); onActivated: root.splitTimelineAtPlayhead() }
    Shortcut { sequence: timelineController.shortcut("duplicate"); enabled:!root.textEditorHasFocus(); onActivated: timelineController.duplicateSelected() }
    Shortcut { sequence: timelineController.shortcut("copy"); enabled:!root.textEditorHasFocus(); onActivated: timelineController.copySelected() }
    Shortcut { sequence: timelineController.shortcut("paste"); enabled:!root.textEditorHasFocus(); onActivated: timelineController.paste() }
    Shortcut { sequence: timelineController.shortcut("group"); enabled:!root.textEditorHasFocus(); onActivated: timelineController.groupSelected() }
    Shortcut { sequence: timelineController.shortcut("unlink"); enabled:!root.textEditorHasFocus(); onActivated: timelineController.unlinkSelected() }
    Shortcut { sequence: "I"; enabled:!root.textEditorHasFocus(); onActivated: root.markInMs = sourcePlayer.position }
    Shortcut { sequence: "O"; enabled:!root.textEditorHasFocus(); onActivated: root.markOutMs = sourcePlayer.position }
    Shortcut { sequence: "M"; enabled:!root.textEditorHasFocus(); onActivated: timelineController.addMarker(timelineController.playheadMs, "Marker") }
    Shortcut { sequence: "N"; enabled:!root.textEditorHasFocus(); onActivated: timelineController.snapping = !timelineController.snapping }
    Shortcut {
        sequence: "C"
        enabled: !root.textEditorHasFocus()
        onActivated: {
            if (timelineController.selectedIds.length > 0)
                timelineController.createLibraryClipFromSelection("")
            else
                projectController.createLibraryClip("", root.markInMs, root.markOutMs)
        }
    }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: !root.textEditorHasFocus()
        onActivated: {
            root.shuttleRate = 0
            sourcePlayer.playbackRate = 1
            root.toggleProgramPlayback()
        }
    }
    Shortcut { sequence: "Ctrl++"; context: Qt.ApplicationShortcut; onActivated: root.zoomTimelineAt(1.25, timelineFlick.width / 2) }
    Shortcut { sequence: "Ctrl+-"; context: Qt.ApplicationShortcut; onActivated: root.zoomTimelineAt(0.8, timelineFlick.width / 2) }
    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: !root.textEditorHasFocus()
        onActivated: root.stepTimelineFrame(-1)
    }
    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: !root.textEditorHasFocus()
        onActivated: root.stepTimelineFrame(1)
    }
    Shortcut {
        sequence: "J"
        enabled: !root.textEditorHasFocus()
        onActivated: {
            if (root.activeViewerIndex === 1) {
                root.stepTimelineFrame(-1)
            } else {
                sourcePlayer.pause()
                root.shuttleRate = root.shuttleRate < 0 ? Math.max(-8, root.shuttleRate * 2) : -1
            }
        }
    }
    Shortcut {
        sequence: "K"
        enabled: !root.textEditorHasFocus()
        onActivated: {
            root.shuttleRate = 0
            sourcePlayer.pause()
            root.stopProgramPlayback()
        }
    }
    Shortcut {
        sequence: "L"
        enabled: !root.textEditorHasFocus()
        onActivated: {
            if (root.activeViewerIndex === 1) {
                root.toggleProgramPlayback()
            } else {
                root.shuttleRate = root.shuttleRate > 0 ? Math.min(8, root.shuttleRate * 2) : 1
                sourcePlayer.playbackRate = root.shuttleRate
                sourcePlayer.play()
            }
        }
    }

    header: NleTopBar {
        theme: theme
        projectName: projectController.projectName
        dirty: projectController.dirty
        canUndo: projectController.canUndo
        canRedo: projectController.canRedo
        busy: projectController.busy
        exportBusy: exportController.busy
        exportEnabled: timelineController.durationMs > 0
        relinkVisible: projectController.hasMissingMedia
        missingMediaName: projectController.missingMediaName
        onNewRequested: root.requestDestructiveAction("new")
        onOpenRequested: root.requestDestructiveAction("open")
        onImportRequested: importDialog.open()
        onSaveRequested: { root.captureSessionState(); if (!projectController.saveProject()) saveDialog.open() }
        onRelinkRequested: relinkDialog.open()
        onUndoRequested: projectController.undo()
        onRedoRequested: projectController.redo()
        onExportRequested: exportDialog.open()
        onShortcutEditorRequested: shortcutEditor.open()
        onGuideRequested: projectController.showFirstRunTutorial()
        onSnapshotRequested: snapshotDialog.open()
        onCommandPaletteRequested: function(scope) { commandPalette.openWithQuery(scope) }
        onToggleLibraryRequested: uiSettings.libraryCollapsed = !uiSettings.libraryCollapsed
        onToggleInspectorRequested: uiSettings.inspectorCollapsed = !uiSettings.inspectorCollapsed
        RowLayout {
            visible: false
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 6
            Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 40; radius: 8; border.color:theme.accentBright;border.width:1
                gradient: Gradient { GradientStop{position:0;color:theme.accentBright}GradientStop{position:1;color:theme.accentDark} }
                Label { anchors.centerIn: parent; text: "Y"; font.pixelSize: 22; font.weight: Font.Black; color: "white" }
            }
            ColumnLayout { spacing: -2; Layout.leftMargin: 4; Layout.rightMargin: 12; visible:root.width>=1180
                Label { text: "YTP EDITOR"; font.weight: Font.Black; font.letterSpacing: 1.2; color: theme.text }
                Label { text: "CREATIVE CUT WORKSTATION"; font.pixelSize: 9; font.letterSpacing: 1.0; color: theme.cyan }
            }
            AppToolButton { text: "+ New"; helpText:"Create a blank project"; enabled: !projectController.busy; onClicked: root.requestDestructiveAction("new") }
            AppToolButton { text: "Open"; helpText:"Open an existing YTP project"; enabled: !projectController.busy; onClicked: root.requestDestructiveAction("open") }
            AppToolButton { text: "Import"; helpText:"Import video or audio into Project Media"; enabled: !projectController.busy; onClicked: importDialog.open() }
            AppToolButton { text: "Save Session"; helpText:"Save the complete edit and workspace state (Ctrl+S)"; onClicked: { root.captureSessionState(); if (!projectController.saveProject()) saveDialog.open() } }
            AppToolButton {
                text: "Relink: " + projectController.missingMediaName
                visible: projectController.hasMissingMedia
                enabled: !projectController.busy
                onClicked: relinkDialog.open()
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
            AppToolButton { text: "↶"; helpText:"Undo (Ctrl+Z)"; font.pixelSize: 20; enabled: projectController.canUndo; onClicked: projectController.undo() }
            AppToolButton { text: "↷"; helpText:"Redo (Ctrl+Shift+Z)"; font.pixelSize: 20; enabled: projectController.canRedo; onClicked: projectController.redo() }
            Label { visible:root.width>=1400;text: projectController.projectName + (projectController.dirty?" •":""); color: projectController.dirty?theme.amber:theme.muted; elide:Text.ElideMiddle; Layout.maximumWidth:140; Layout.leftMargin:6 }
            Item { Layout.fillWidth: true }
            Label { visible:root.width>=1250;text: "WORKSPACE"; color: theme.faint; font.pixelSize: 9; font.weight: Font.DemiBold }
            ComboBox { model: ["YTP Focus", "Cut & Arrange", "Audio Lab"]; currentIndex: uiSettings.workspaceMode; onActivated: { uiSettings.workspaceMode=currentIndex; inspectorTabs.currentIndex=currentIndex===0?3:currentIndex===1?1:2 } Layout.preferredWidth: 128; ToolTip.text: "Workspace layout preset" }
            AppToolButton { text: "Keys"; helpText:"View or change keyboard shortcuts"; onClicked: shortcutEditor.open() }
            AppToolButton { text: "Guide"; helpText:"Open the guided editor tour"; visible:root.width>=1100;onClicked: projectController.showFirstRunTutorial() }
            AppToolButton { text: "Snapshot"; helpText:"Export the current Program frame as an image"; visible:root.width>=1300;enabled: timelineController.durationMs > 0; onClicked: snapshotDialog.open() }
            AccentButton { text: exportController.busy ? "Rendering…" : "Export"; enabled: timelineController.durationMs > 0; onClicked: exportDialog.open(); Layout.preferredWidth: 92 }
        }
    }

    footer: StatusStrip {
        theme: theme
        busy: projectController.busy
        dirty: projectController.dirty
        detailText: projectController.statusMessage
        RowLayout {
            visible: false
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            Rectangle { width: 7; height: 7; radius: 4; color: projectController.busy ? theme.amber : theme.green }
            Label { text: projectController.statusMessage; color: theme.muted; Layout.fillWidth: true }
            BusyIndicator { running: projectController.busy; visible: running; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
            Pill { visible: projectController.dirty; text: "RECOVERY ACTIVE"; color: theme.cyan }
            Pill { text: projectController.dirty ? "UNSAVED" : "SAVED"; color: projectController.dirty ? theme.amber : theme.green }
        }
    }

    CommandPalette {
        id: commandPalette
        theme: theme
        onCommandTriggered: function(command) { root.executePaletteCommand(command) }
    }
    ToastOverlay {
        id: toastOverlay
        theme: theme
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        z: 10000
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        SplitView {
            SplitView.fillHeight: true
            SplitView.minimumHeight: root.height < 720 ? 220 : 300

            Panel {
                id: mediaPanel
                objectName: "mediaPanel"
                visible: !uiSettings.libraryCollapsed
                SplitView.preferredWidth: 250
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 340
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 38; Layout.leftMargin: 10; Layout.rightMargin: 6
                        Label { text: libraryTabs.currentIndex === 0 ? "Media" : "Reusable clips"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        AppToolButton { text: "+"; helpText: "Import media (Ctrl+I)"; onClicked: importDialog.open() }
                        AppToolButton { text: "‹"; helpText: "Collapse media panel"; onClicked: uiSettings.libraryCollapsed = true }
                    }
                    TabBar {
                        id: libraryTabs
                        objectName: "libraryTabs"
                        Layout.fillWidth: true
                        background: Rectangle { color: theme.canvas }
                        WorkspaceTab { text: "Media" }
                        WorkspaceTab { text: "Clips" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 6
                        Layout.rightMargin: 6
                        Layout.preferredHeight: 34
                        TextField {
                            placeholderText: libraryTabs.currentIndex === 0 ? "Search media…" : "Search clips, tags, notes…"
                            Layout.fillWidth: true
                            onTextChanged: {
                                if (libraryTabs.currentIndex === 1)
                                    projectController.clipLibrary.filterText = text
                                else
                                    projectController.mediaLibrary.filterText = text
                            }
                        }
                        ToolButton {
                            text: "★"
                            checkable: true
                            onCheckedChanged: projectController.clipLibrary.favoritesOnly = checked
                        }
                    }
                    RowLayout {
                        visible: false
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        ComboBox {
                            model: ["Recent", "Recently used", "Name", "Duration"]
                            Layout.fillWidth: true
                            onCurrentTextChanged: projectController.clipLibrary.sortMode = currentText
                        }
                        TextField {
                            placeholderText: "Filter bin"
                            Layout.fillWidth: true
                            onTextChanged: projectController.clipLibrary.binFilter = text
                        }
                    }
                    RowLayout {
                        visible: false
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        Label { text: "Bin"; color: theme.muted }
                        TextField {
                            placeholderText: "Filter bin"
                            Layout.fillWidth: true
                            onTextChanged: projectController.mediaLibrary.binFilter = text
                        }
                    }
                    GridView {
                        id: clipGrid
                        objectName: "clipGrid"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 6
                        model: projectController.clipLibrary
                        visible: libraryTabs.currentIndex === 1
                        cellWidth: width
                        cellHeight: 154
                        clip: !root.libraryClipDragActive
                        delegate: Rectangle {
                            required property string clipName
                            required property string clipColor
                            required property string clipDuration
                            required property url thumbnailUrl
                            required property string clipId
                            required property var clipTags
                            required property string clipNotes
                            required property bool clipFavorite
                            required property string clipBin
                            required property int sourceStartMs
                            required property int sourceEndMs
                            objectName: "libraryClip_" + clipId
                            width: clipGrid.cellWidth - 6
                            height: clipGrid.cellHeight - 6
                            opacity: clipDrag.active ? .55 : 1
                            radius: 3
                            color: clipDrag.active ? theme.panelHover : theme.panelRaised
                            border.color: clipDrag.active ? theme.accent : theme.border
                            border.width: clipDrag.active ? 2 : 1
                            DragHandler {
                                id: clipDrag
                                target: null
                                function updateProxyPosition() {
                                    const point = parent.mapToItem(root.contentItem,
                                        centroid.position.x, centroid.position.y)
                                    root.libraryClipDragX = point.x
                                    root.libraryClipDragY = point.y
                                }
                                onTranslationChanged: updateProxyPosition()
                                onActiveChanged: {
                                    if (active) {
                                        root.libraryClipDragId = parent.clipId
                                        root.libraryClipDragName = parent.clipName
                                        updateProxyPosition()
                                        root.libraryClipDragActive = true
                                    } else {
                                        // DragHandler release does not synthesize a Drop event
                                        // for a proxy item; explicitly complete the active drag.
                                        libraryClipDragProxy.Drag.drop()
                                        root.libraryClipDragActive = false
                                        // Drag.active becoming false dispatches the drop. Keep
                                        // the payload alive until that event has been delivered.
                                        Qt.callLater(function() {
                                            root.libraryClipDragId = ""
                                            root.libraryClipDragName = ""
                                        })
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: projectController.activateLibraryClip(clipId)
                                onDoubleClicked: root.editClip(clipId, clipName, clipTags, clipNotes,
                                                               clipColor, clipBin, clipFavorite,
                                                               sourceStartMs, sourceEndMs)
                            }
                            Image {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                height: 100
                                source: thumbnailUrl
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                Rectangle { anchors.fill: parent; color: theme.canvas; z: -1 }
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 28; gradient: Gradient { GradientStop { position: 0; color: "transparent" } GradientStop { position: 1; color: "#cc090b10" } } }
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 7
                                anchors.bottomMargin: 25
                                text: clipName
                                elide: Text.ElideRight
                                color: "#eef0f4"
                                z: 2
                            }
                            Label { anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;anchors.margins:7;text:clipTags.length>0?clipTags.join(", "):"No tags";elide:Text.ElideRight;color:theme.muted;font.pixelSize:10 }
                            Label { anchors.right:parent.right;anchors.top:parent.top;anchors.margins:6;text:clipDuration;color:"white";style:Text.Outline;styleColor:"black";z:3;font.pixelSize:10 }
                            Label {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 6
                                text: root.formatTime(sourceStartMs) + "–" + root.formatTime(sourceEndMs)
                                color: "white"
                                style: Text.Outline
                                styleColor: "black"
                                z: 3
                                font.pixelSize: 10
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 5
                                text: clipFavorite ? "STAR" : clipBin
                                color: clipFavorite ? "#ffd166" : "#d8dbe2"
                                style: Text.Outline
                                styleColor: "#000000"
                                z: 2
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: clipGrid.count === 0
                            width: parent.width - 30
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: projectController.sourceUrl.toString() === ""
                                  ? "Import a video, mark In and Out, then press C"
                                  : "Mark In and Out in the Source Viewer, then create a reusable clip"
                            color: "#838b99"
                        }
                    }
                    ListView {
                        id: mediaList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 6
                        visible: libraryTabs.currentIndex === 0
                        clip: true
                        spacing: 5
                        model: projectController.mediaLibrary
                        delegate: Rectangle {
                            required property string mediaId
                            required property string mediaName
                            required property string mediaPath
                            required property string mediaDuration
                            required property string mediaResolution
                            required property string mediaBin
                            required property bool mediaMissing
                            required property url mediaThumbnailUrl
                            width: mediaList.width
                            height: 70
                            radius: 3
                            color: mediaMissing ? "#2b1b21" : mediaHover.containsMouse ? theme.panelHover : "transparent"
                            border.color: mediaMissing ? theme.danger : "transparent"
                            MouseArea {
                                id: mediaHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onDoubleClicked: {
                                    projectController.activateMedia(mediaId)
                                    viewerTabs.currentIndex = 0
                                }
                            }
                            Image {
                                anchors.left: parent.left; anchors.leftMargin: 5
                                anchors.verticalCenter: parent.verticalCenter
                                width: 88; height: 50
                                source: mediaThumbnailUrl
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                Rectangle { anchors.fill: parent; z: -1; color: theme.canvas; border.color: theme.border }
                            }
                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 102
                                anchors.right: mediaMenuButton.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                Label { text: root.readableClipName(mediaName); width: parent.width; elide: Text.ElideRight; color: theme.text; font.weight: Font.Medium }
                                Label { text: mediaDuration + "  •  " + mediaResolution + (mediaMissing ? "  •  MISSING" : ""); color: mediaMissing ? "#ff9aa8" : "#929aa8"; font.pixelSize: 10 }
                            }
                            ToolButton {
                                id: mediaMenuButton
                                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4
                                text: "⋮"; width: 28; height: 28
                                onClicked: mediaMenu.popup()
                                Menu { id: mediaMenu
                                    MenuItem { text: "Open in Source"; onTriggered: { projectController.activateMedia(mediaId); viewerTabs.currentIndex = 0 } }
                                    MenuItem { text: "Create Proxy"; onTriggered: projectController.generateProxy(mediaId) }
                                    MenuItem { text: "Relink…"; visible: mediaMissing; onTriggered: relinkDialog.open() }
                                    MenuSeparator {}
                                    MenuItem { text: "Properties"; onTriggered: toastOverlay.show(mediaName + "  •  " + mediaResolution + "  •  " + mediaDuration) }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: mediaList.count === 0
                            text: "No project media"
                            color: "#838b99"
                        }
                    }
                }
            }

            Panel {
                objectName: "viewerPanel"
                SplitView.fillWidth: true
                SplitView.minimumWidth: 500
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    TabBar {
                        id: viewerTabs
                        objectName: "viewerTabs"
                        currentIndex: 1
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 30 : 0
                        visible: currentIndex !== 1
                        background: Rectangle { color: theme.canvas }
                        WorkspaceTab { text: "Source" }
                        WorkspaceTab { text: "Program" }
                        WorkspaceTab { text: "Dual"; visible: root.width >= 1400 }
                    }
                    RowLayout { objectName: "programPreviewOptions"; Layout.fillWidth: true; visible: root.viewerOptionsPinned || timelineController.previewRendering
                        Label { text:"Preview quality";Layout.leftMargin:8;color:"#929aa8" }
                        ComboBox { model:["Auto","Full","1/2","1/4","Proxy"];onActivated:timelineController.setPreviewQuality(currentIndex) }
                        Button { text: timelineController.previewRendering ? "Rendering…" : "Build A/V Preview"; enabled:!timelineController.previewRendering;onClicked:timelineController.renderPlaybackPreview() }
                ToolButton { text:root.programPlaybackRequested?"Pause":"Play";enabled:timelineController.durationMs>0;onClicked:root.toggleProgramPlayback() }
                        Button { text:timelineController.previewRendering?"Caching "+Math.round(timelineController.programCacheProgress*100)+"%":(timelineController.programCacheStale?"Continuous":"Refresh");enabled:!timelineController.previewRendering;onClicked:{timelineController.setContinuousCaching(true);timelineController.renderContinuousProgramCache()} }
                        Label{text:"Frames "+root.programFrames+" / dropped "+root.programDroppedFrames;color:root.programDroppedFrames>0?"#ffca70":"#7fd6a6";font.pixelSize:10}
                        Item { Layout.fillWidth:true }
                    }
                    Rectangle {
                        id: programMonitor
                        objectName: "programMonitor"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.canvas
                        border.width: 0
                        Item {
                            id: sourceVideo
                            objectName: "sourceVideoOutput"
                            x: 0; y: 0
                            width: root.activeViewerIndex === 2 ? parent.width/2 : parent.width
                            height: parent.height
                            visible: (root.activeViewerIndex === 0 || root.activeViewerIndex === 2) && projectController.sourceUrl.toString() !== ""
                            Loader {
                                anchors.fill: parent
                                active: sourcePlayer.videoWindow !== null
                                sourceComponent: Component { WindowContainer { window: sourcePlayer.videoWindow } }
                            }
                        }
                        Image {
                            x: root.activeViewerIndex===2 ? parent.width/2 : 0
                            y: 0
                            width: root.activeViewerIndex===2 ? parent.width/2 : parent.width
                            height: parent.height
                            visible: (root.activeViewerIndex === 1 || root.activeViewerIndex === 2) && !root.instantPreviewActive && timelineController.programImageUrl.toString() !== "" && !root.programPreviewContains(timelineController.playheadMs)
                            source: timelineController.programImageUrl
                            fillMode: Image.PreserveAspectFit
                        }
                        Item { id:programVideo;objectName:"programVideoOutput";x:root.activeViewerIndex===2?parent.width/2:0;y:0;width:root.activeViewerIndex===2?parent.width/2:parent.width;height:parent.height;visible:false }
                        Item { id:instantProgramVideo;objectName:"instantProgramVideoOutput";x:root.activeViewerIndex===2?parent.width/2:0;y:0;width:root.activeViewerIndex===2?parent.width/2:parent.width;height:parent.height;visible:root.instantPreviewActive&&(root.activeViewerIndex===1||root.activeViewerIndex===2);z:3;Loader{anchors.fill:parent;active:instantProgramPlayer.videoWindow!==null;sourceComponent:Component{WindowContainer{window:instantProgramPlayer.videoWindow}}} }
                        Rectangle { anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;width:draftPlaybackLabel.implicitWidth+18;height:26;radius:13;color:"#cc171c26";border.color:theme.amber;visible:root.instantPlaybackRequested&&!root.timelineClockPreviewExact;z:4;Label{id:draftPlaybackLabel;anchors.centerIn:parent;text:"DIRECT DRAFT — EFFECTS UPDATE WHEN PAUSED";font.pixelSize:9;font.weight:Font.Bold;color:theme.amber} }
                        Rectangle { anchors.right:parent.right;anchors.bottom:parent.bottom;anchors.margins:10;width:syncingLabel.implicitWidth+18;height:28;radius:14;color:"#cc171c26";border.color:theme.borderStrong;visible:timelineController.previewRendering&&!root.programPlaybackRequested&&!root.instantPreviewActive;Label{id:syncingLabel;anchors.centerIn:parent;text:"BUILDING PREVIEW";font.pixelSize:10;font.weight:Font.Bold;font.letterSpacing:1;color:theme.cyan} }
                        Rectangle { x:parent.width/2;width:1;height:parent.height;color:theme.borderStrong;visible:root.activeViewerIndex===2 }
                        Label {
                            anchors.centerIn: parent
                            visible: root.activeViewerIndex !== 2 && (root.activeViewerIndex === 0 ? !sourceVideo.visible : (timelineController.programImageUrl.toString() === "" && !root.programPreviewContains(timelineController.playheadMs)))
                            text: root.activeViewerIndex === 0 ? "Import media to begin" : "Move the timeline playhead onto a video clip"
                            color: theme.faint
                            font.pixelSize: 17
                            font.weight: Font.Medium
                        }
                        Label { x:0;width:parent.width/2;anchors.verticalCenter:parent.verticalCenter;horizontalAlignment:Text.AlignHCenter;visible:root.activeViewerIndex===2&&!sourceVideo.visible;text:"SOURCE\nImport media to begin";color:theme.faint;lineHeight:1.5 }
                        Label { x:parent.width/2;width:parent.width/2;anchors.verticalCenter:parent.verticalCenter;horizontalAlignment:Text.AlignHCenter;visible:root.activeViewerIndex===2&&timelineController.programImageUrl.toString()===""&&timelineController.playbackPreviewUrl.toString()==="";text:"PROGRAM\nPlace the playhead over a clip";color:theme.faint;lineHeight:1.5 }
                        Label { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 7; visible: root.activeViewerIndex === 1 && !root.focusProgramMode; text: timelineController.programLabel; color: "#b8bec9"; font.pixelSize: 10; style: Text.Outline; styleColor: "black" }
                        Label {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 7
                            text: "SOURCE"
                            visible: sourceVideo.visible
                            color: "#b8bec9"
                            font.pixelSize: 10
                            style: Text.Outline
                            styleColor: "#000000"
                        }
                    }
                    Rectangle {
                        objectName: "sourceWaveformStrip"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        visible: root.activeViewerIndex !== 1
                        color: theme.canvas
                        Image {
                            anchors.fill: parent
                            anchors.margins: 3
                            source: projectController.sourceWaveformUrl
                            fillMode: Image.Stretch
                            opacity: 0.8
                        }
                        Slider {
                            anchors.fill: parent
                            from: 0
                            to: Math.max(1, sourcePlayer.duration)
                            value: sourcePlayer.position
                            onMoved: sourcePlayer.position = value
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.preferredHeight: 38
                        Label { text: root.formatTime(root.activeViewerIndex===1?timelineController.playheadMs:sourcePlayer.position); font.family: "Consolas"; color: "#f0f1f4" }
                        Label { visible:root.activeViewerIndex!==1;text: "In " + root.formatTime(root.markInMs); color: "#7fd6a6" }
                        Label { visible:root.activeViewerIndex!==1;text: "Out " + root.formatTime(root.markOutMs); color: "#ff9ac2" }
                        Label { visible:false;text:timelineController.playbackPreviewUrl.toString()!==""?"Program preview":"Program frame";color:theme.cyan;font.weight:Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        ToolButton { visible:root.activeViewerIndex===1;text:"⋯";implicitWidth:30;implicitHeight:28;ToolTip.text:"Preview quality and cache options";onClicked:root.viewerOptionsPinned=!root.viewerOptionsPinned }
                        ToolButton { visible:root.activeViewerIndex!==1;text: "I"; ToolTip.text: "Mark In"; onClicked: root.markInMs = sourcePlayer.position }
                ToolButton { text: "-1f";ToolTip.text:"Step the active viewer back one frame";onClicked:root.activeViewerIndex===1?timelineController.playheadMs=timelineController.stepFrame(timelineController.playheadMs,-1):sourcePlayer.position=projectController.stepFrame(sourcePlayer.position,-1) }
                        ToolButton {
                            text: root.activeViewerIndex===1?(root.programPlaybackRequested?"Pause":(timelineController.previewRendering?"Building…":"Play")):(sourcePlayer.playbackState===NativeMediaPlayer.PlayingState?"Pause":"Play")
                            enabled:root.activeViewerIndex!==1||timelineController.durationMs>0
                            ToolTip.text:root.activeViewerIndex===1?"Play the timeline Program preview; builds the active clip preview when needed":"Play or pause the Source viewer"
                            onClicked:root.toggleViewerPlayback()
                        }
                ToolButton { text: "+1f";ToolTip.text:"Step the active viewer forward one frame";onClicked:root.activeViewerIndex===1?timelineController.playheadMs=timelineController.stepFrame(timelineController.playheadMs,1):sourcePlayer.position=projectController.stepFrame(sourcePlayer.position,1) }
                        ToolButton { visible:root.activeViewerIndex!==1;text: "O"; ToolTip.text: "Mark Out"; onClicked: root.markOutMs = sourcePlayer.position }
                        Button {
                            visible:root.activeViewerIndex!==1
                            text: "Create Clip (C)"
                            highlighted: true
                            implicitHeight: 30
                            enabled: projectController.sourceUrl.toString() !== "" && root.markOutMs > root.markInMs
                            onClicked: projectController.createLibraryClip("", root.markInMs, root.markOutMs)
                        }
                    }
                }
            }

            Panel {
                objectName: "inspectorPanel"
                visible: !uiSettings.inspectorCollapsed
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 260
                SplitView.maximumWidth: 380
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout { Layout.fillWidth:true;spacing:0
                    TabBar { id: inspectorTabs; objectName: "inspectorTabs"; Layout.fillWidth: true; background: Rectangle { color: theme.canvas }
                        onCurrentIndexChanged: {
                            if (currentIndex === 0) root.inspectorContentIndex = 1
                            else if (currentIndex === 1) root.inspectorContentIndex = 6
                            else root.inspectorContentIndex = 3
                        }
                        WorkspaceTab { text:"Inspector";helpText:"Properties for the selected timeline event" }
                        WorkspaceTab { text:"Effects";helpText:"Effects and presets for the selection" }
                        WorkspaceTab { text:"YTP";helpText:"Fast repeat, reverse, remix, and distortion tools" }
                    }
                    ToolButton { text:"⋮";Layout.preferredWidth:32;ToolTip.text:"More editor panels";onClicked:inspectorMore.popup();Menu{id:inspectorMore
                        MenuItem{text:"Audio Mixer";onTriggered:{inspectorTabs.currentIndex=0;root.inspectorContentIndex=2}}
                        MenuItem{text:"Finish & Captions";onTriggered:{root.inspectorContentIndex=4;inspectorTabs.currentIndex=2}}
                        MenuItem{text:"Remix Lab";onTriggered:{root.inspectorContentIndex=5;inspectorTabs.currentIndex=2}}
                        MenuSeparator{}
                        MenuItem{text:"Collapse Inspector";onTriggered:uiSettings.inspectorCollapsed=true}
                    }}
                    }
                    StackLayout { id:inspectorStack;Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; clip: true; currentIndex: root.inspectorContentIndex
                        ColumnLayout { spacing:0
                            WorkspaceHeader { heading:"Source clips";description:"Mark a moment once, then reuse it everywhere" }
                            Flickable { id:sourceInspectorPages;objectName:"sourceInspectorPages";clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;contentWidth:width;contentHeight:Math.max(height,sourceInspectorColumn.implicitHeight);boundsBehavior:Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded;interactive:true}
                                WheelHandler{target:null;blocking:true;onWheel:function(event){var d=event.pixelDelta.y!==0?event.pixelDelta.y:event.angleDelta.y/120*48;sourceInspectorPages.contentY=Math.max(0,Math.min(sourceInspectorPages.contentHeight-sourceInspectorPages.height,sourceInspectorPages.contentY-d));event.accepted=true}}
                                ColumnLayout { id:sourceInspectorColumn;width:sourceInspectorPages.width;spacing:8
                            InspectorCard { title:"Active media"
                                ColumnLayout { anchors.fill:parent;spacing:10
                                    RowLayout { Layout.fillWidth:true
                                        Rectangle { width:42;height:42;radius:9;color:projectController.sourceName?"#26364b":theme.canvas;border.color:theme.borderStrong;Label{anchors.centerIn:parent;text:projectController.sourceName?"▶":"—";color:projectController.sourceName?theme.cyan:theme.faint;font.pixelSize:18} }
                                        ColumnLayout { Layout.fillWidth:true;spacing:2
                                            Label { text:projectController.sourceName||"No media selected";Layout.fillWidth:true;elide:Text.ElideMiddle;font.weight:Font.DemiBold;color:theme.text }
                                            Label { text:projectController.sourceName?"Ready for In / Out marking":"Import or choose media from the Library";color:theme.muted;font.pixelSize:11 }
                                        }
                                    }
                                    RowLayout { Layout.fillWidth:true
                                        ColumnLayout { Layout.fillWidth:true;spacing:2;Label{text:"DURATION";font.pixelSize:9;color:theme.faint;font.weight:Font.Bold}Label{text:root.formatTime(projectController.sourceDurationMs);font.family:"Consolas";color:theme.muted} }
                                        ColumnLayout { Layout.fillWidth:true;spacing:2;Label{text:"SELECTION";font.pixelSize:9;color:theme.faint;font.weight:Font.Bold}Label{text:root.formatTime(Math.max(0,root.markOutMs-root.markInMs));font.family:"Consolas";color:theme.accent} }
                                    }
                                }
                            }
                            InspectorCard { title:"Save reusable clip"
                                ColumnLayout { anchors.fill:parent;spacing:9
                                    Label { text:"The current In/Out range becomes a draggable library clip.";Layout.fillWidth:true;wrapMode:Text.Wrap;color:theme.muted;font.pixelSize:11 }
                                    TextField { id:clipNameField;placeholderText:"Clip name (optional)";Layout.fillWidth:true }
                                AccentButton { text:"Create Clip";helpText:"Save the selected timeline segment, or the Source In/Out range when nothing is selected (C)";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0||(projectController.sourceUrl.toString()!==""&&root.markOutMs>root.markInMs);onClicked:{const ok=timelineController.selectedIds.length>0?timelineController.createLibraryClipFromSelection(clipNameField.text):projectController.createLibraryClip(clipNameField.text,root.markInMs,root.markOutMs);if(ok)clipNameField.clear()} }
                                    Label { text:"Shortcut: C";Layout.alignment:Qt.AlignHCenter;color:theme.faint;font.pixelSize:10 }
                                }
                            }
                                }
                            }
                        }
                        ColumnLayout { spacing:0
                            WorkspaceHeader { heading:"Inspector";description:timelineController.inspector.itemId?"Selected event properties":"Select a timeline event" }
                            SectionPicker { id:editSection;objectName:"editSection";model:["Position & Scale","Crop & Flip","Timing & Audio","Captions","Masks","Applied Effects"];onCurrentIndexChanged:editInspectorPages.contentY=0 }
                            Flickable { id:editInspectorPages;objectName:"editInspectorPages";clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;contentWidth:width;contentHeight:Math.max(height,editInspectorColumn.implicitHeight);boundsBehavior:Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded;interactive:true}
                                WheelHandler{target:null;blocking:true;onWheel:function(event){var d=event.pixelDelta.y!==0?event.pixelDelta.y:event.angleDelta.y/120*48;editInspectorPages.contentY=Math.max(0,Math.min(editInspectorPages.contentHeight-editInspectorPages.height,editInspectorPages.contentY-d));event.accepted=true}}
                                ColumnLayout { id:editInspectorColumn;width:editInspectorPages.width;spacing:3
                                InspectorCard { title:"Position & Scale";visible:editSection.currentIndex===0;enabled:!!timelineController.inspector.itemId
                                    GridLayout { columns:4;anchors.fill:parent;columnSpacing:5;rowSpacing:3
                                        Label{text:"X"} SpinBox{Layout.fillWidth:true;from:-10000;to:10000;value:timelineController.inspector.positionX||0;editable:true;onValueModified:timelineController.setTransformValue("positionX",value)}
                                        Label{text:"Y"} SpinBox{Layout.fillWidth:true;from:-10000;to:10000;value:timelineController.inspector.positionY||0;editable:true;onValueModified:timelineController.setTransformValue("positionY",value)}
                                        Label{text:"Scale X"} SpinBox{Layout.fillWidth:true;from:1;to:1000;value:(timelineController.inspector.scaleX||1)*100;onValueModified:timelineController.setTransformValue("scaleX",value/100)}
                                        Label{text:"Scale Y"} SpinBox{Layout.fillWidth:true;from:1;to:1000;value:(timelineController.inspector.scaleY||1)*100;onValueModified:timelineController.setTransformValue("scaleY",value/100)}
                                        Label{text:"Rotation"} SpinBox{Layout.fillWidth:true;from:-3600;to:3600;value:timelineController.inspector.rotation||0;editable:true;onValueModified:timelineController.setTransformValue("rotation",value)}
                                        Label{text:"Opacity"} SpinBox{Layout.fillWidth:true;from:0;to:100;value:(timelineController.inspector.opacity===undefined?1:timelineController.inspector.opacity)*100;onValueModified:timelineController.setTransformValue("opacity",value/100)}
                                        Label{text:"Anchor X"} SpinBox{Layout.fillWidth:true;from:0;to:100;value:(timelineController.inspector.anchorX===undefined?.5:timelineController.inspector.anchorX)*100;onValueModified:timelineController.setTransformValue("anchorX",value/100)}
                                        Label{text:"Anchor Y"} SpinBox{Layout.fillWidth:true;from:0;to:100;value:(timelineController.inspector.anchorY===undefined?.5:timelineController.inspector.anchorY)*100;onValueModified:timelineController.setTransformValue("anchorY",value/100)}
                                        Button{text:"◆ Position key";Layout.columnSpan:2;Layout.fillWidth:true;onClicked:{timelineController.addTransformKeyframe("positionX",timelineController.playheadMs,timelineController.inspector.positionX||0,1);timelineController.addTransformKeyframe("positionY",timelineController.playheadMs,timelineController.inspector.positionY||0,1)}}
                                        Button{text:"◆ Opacity key";Layout.columnSpan:2;Layout.fillWidth:true;onClicked:timelineController.addTransformKeyframe("opacity",timelineController.playheadMs,timelineController.inspector.opacity===undefined?1:timelineController.inspector.opacity,1)}
                                    }
                                }
                                InspectorCard { title:"Crop & Flip";visible:editSection.currentIndex===1;enabled:!!timelineController.inspector.itemId
                                    GridLayout { columns:4;anchors.fill:parent;columnSpacing:5;rowSpacing:3
                                        Label{text:"Left"} SpinBox{Layout.fillWidth:true;from:0;to:99;value:(timelineController.inspector.cropLeft||0)*100;onValueModified:timelineController.setTransformValue("cropLeft",value/100)}
                                        Label{text:"Top"} SpinBox{Layout.fillWidth:true;from:0;to:99;value:(timelineController.inspector.cropTop||0)*100;onValueModified:timelineController.setTransformValue("cropTop",value/100)}
                                        Label{text:"Right"} SpinBox{Layout.fillWidth:true;from:0;to:99;value:(timelineController.inspector.cropRight||0)*100;onValueModified:timelineController.setTransformValue("cropRight",value/100)}
                                        Label{text:"Bottom"} SpinBox{Layout.fillWidth:true;from:0;to:99;value:(timelineController.inspector.cropBottom||0)*100;onValueModified:timelineController.setTransformValue("cropBottom",value/100)}
                                        CheckBox{text:"Fit frame";checked:timelineController.inspector.fit===undefined?true:timelineController.inspector.fit;onToggled:timelineController.setTransformFlag("fit",checked);Layout.columnSpan:2}
                                        CheckBox{text:"Flip horizontal";checked:timelineController.inspector.flipHorizontal||false;onToggled:timelineController.setTransformFlag("flipHorizontal",checked);Layout.columnSpan:2}
                                        CheckBox{text:"Flip vertical";checked:timelineController.inspector.flipVertical||false;onToggled:timelineController.setTransformFlag("flipVertical",checked);Layout.columnSpan:2}
                                    }
                                }
                                InspectorCard { title: "Timing / Audio"; visible:editSection.currentIndex===2; enabled: !!timelineController.inspector.itemId
                                    GridLayout { columns:4;anchors.fill:parent;columnSpacing:5;rowSpacing:3
                                        Label{text:"Speed"} SpinBox{Layout.fillWidth:true;from:1;to:10000;value:(timelineController.inspector.speed||1)*100;onValueModified:timelineController.setSpeed(value/100,preservePitch.checked)}
                                        Label{text:"Pitch"} SpinBox{Layout.fillWidth:true;from:-48;to:48;value:timelineController.inspector.pitch||0;onValueModified:timelineController.setPitch(value)}
                                        Label{text:"Gain dB"} SpinBox{id:clipGain;Layout.fillWidth:true;from:-96;to:24;value:timelineController.inspector.gainDb||0;onValueModified:timelineController.setClipAudio(value,clipPan.value/100)}
                                        Label{text:"Pan"} SpinBox{id:clipPan;Layout.fillWidth:true;from:-100;to:100;value:(timelineController.inspector.pan||0)*100;onValueModified:timelineController.setClipAudio(clipGain.value,value/100)}
                                        CheckBox{id:preservePitch;text:"Preserve pitch";checked:timelineController.inspector.preservePitch===undefined?true:timelineController.inspector.preservePitch;Layout.columnSpan:2}
                                        CheckBox{text:"Reverse";checked:timelineController.inspector.reverse||false;onToggled:timelineController.setReverse(checked);Layout.columnSpan:2}
                                        CheckBox{text:"Freeze at playhead";checked:timelineController.inspector.freeze||false;onToggled:timelineController.setFreeze(checked,(timelineController.inspector.sourceStartMs||0)+Math.max(0,timelineController.playheadMs-(timelineController.inspector.startMs||0)));Layout.columnSpan:4}
                                        Button{text:"◆ Gain key";Layout.columnSpan:2;Layout.fillWidth:true;onClicked:timelineController.addAudioKeyframe(0,timelineController.inspector.itemId,"gain",timelineController.playheadMs,clipGain.value,1)}
                                        Button{text:"◆ Pan key";Layout.columnSpan:2;Layout.fillWidth:true;onClicked:timelineController.addAudioKeyframe(0,timelineController.inspector.itemId,"pan",timelineController.playheadMs,clipPan.value/100,1)}
                                    }
                                }
                                InspectorCard { title:"Caption";visible:editSection.currentIndex===3;enabled:!!timelineController.inspector.itemId
                                    GridLayout { anchors.fill:parent;columns:2
                                        CheckBox{id:captionEnabled;text:"Render caption";checked:timelineController.inspector.captionEnabled||false;Layout.columnSpan:2}
                                        Label{text:"Text"} TextField{id:captionText;text:timelineController.inspector.captionText||"";Layout.fillWidth:true}
                                        Label{text:"Size"} SpinBox{id:captionSize;from:12;to:200;value:timelineController.inspector.captionSize||54}
                                        Label{text:"Color"} TextField{id:captionColor;text:timelineController.inspector.captionColor||"white";placeholderText:"white or #RRGGBB";Layout.fillWidth:true}
                                        Button{text:"Apply Caption";Layout.columnSpan:2;Layout.fillWidth:true;onClicked:timelineController.setCaption(captionEnabled.checked,captionText.text,captionSize.value,captionColor.text)}
                                    }
                                }
                                InspectorCard { title:"Masks";visible:editSection.currentIndex===4;enabled:!!timelineController.inspector.itemId
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Button{text:"+ Rectangle";onClicked:timelineController.addMask(0)} Button{text:"+ Ellipse";onClicked:timelineController.addMask(1)} }
                                        Repeater { model:timelineController.inspector.masks||[]
                                            GridLayout { id:maskRow;required property var modelData;columns:4;Layout.fillWidth:true
                                                Label{text:modelData.shape===0?"Rectangle":"Ellipse";Layout.columnSpan:3} ToolButton{text:"X";onClicked:timelineController.removeMask(maskRow.modelData.id)}
                                                Label{text:"X %"} SpinBox{id:maskX;from:0;to:99;value:maskRow.modelData.x*100} Label{text:"Y %"} SpinBox{id:maskY;from:0;to:99;value:maskRow.modelData.y*100}
                                                Label{text:"W %"} SpinBox{id:maskW;from:1;to:100;value:maskRow.modelData.width*100} Label{text:"H %"} SpinBox{id:maskH;from:1;to:100;value:maskRow.modelData.height*100}
                                                Label{text:"Feather %"} SpinBox{id:maskFeather;from:0;to:100;value:maskRow.modelData.feather*100} Label{text:"Opacity %"} SpinBox{id:maskOpacity;from:0;to:100;value:maskRow.modelData.opacity*100}
                                                CheckBox{id:maskInvert;text:"Invert";checked:maskRow.modelData.inverted} Button{text:"Apply";onClicked:timelineController.updateMask(maskRow.modelData.id,maskX.value/100,maskY.value/100,maskW.value/100,maskH.value/100,maskFeather.value/100,maskOpacity.value/100,maskInvert.checked)} Button{text:"Track";ToolTip.text:"Track this region and generate editable keyframes";onClicked:timelineController.trackMask(maskRow.modelData.id)} Item{}
                                                Button{text:"Attach Clip";ToolTip.text:"Move this clip with the completed track";onClicked:timelineController.applyTrackedMotion(maskRow.modelData.id,0)} Button{text:"Stabilize";ToolTip.text:"Apply inverse tracked motion";onClicked:timelineController.applyTrackedMotion(maskRow.modelData.id,1)} Item{Layout.columnSpan:2}
                                            }
                                        }
                                    }
                                }
                                ColumnLayout {
                                    objectName: "appliedEffectStack"
                                    visible: editSection.currentIndex === 5
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Label {
                                        visible: (timelineController.inspector.effects || []).length === 0
                                        Layout.fillWidth: true
                                        Layout.margins: 12
                                        text: timelineController.inspector.itemId
                                            ? "No effects applied. Add one from the Effects tab."
                                            : "Select a timeline event to inspect its effects."
                                        wrapMode: Text.WordWrap
                                        color: theme.muted
                                    }
                                    Repeater {
                                        model: timelineController.inspector.effects || []
                                        Rectangle {
                                            id: appliedEffect
                                            objectName: "appliedEffectSection"
                                            required property var modelData
                                            required property int index
                                            property bool expanded: index === 0
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 6
                                            Layout.rightMargin: 6
                                            implicitHeight: effectColumn.implicitHeight + 2
                                            color: theme.panelRaised
                                            border.color: theme.border

                                            ColumnLayout {
                                                id: effectColumn
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 1
                                                spacing: 0
                                                Rectangle {
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 30
                                                    color: effectHeaderHover.containsMouse ? theme.panelHover : "transparent"
                                                    RowLayout {
                                                        anchors.fill: parent
                                                        spacing: 2
                                                        ToolButton { text: appliedEffect.expanded ? "▾" : "▸"; implicitWidth: 26; implicitHeight: 26; onClicked: appliedEffect.expanded = !appliedEffect.expanded }
                                                        CheckBox { checked: appliedEffect.modelData.enabled; implicitWidth: 26; ToolTip.text: "Enable effect"; onToggled: timelineController.bypassEffect(0,timelineController.inspector.itemId,appliedEffect.modelData.id,!checked) }
                                                        Label { text: appliedEffect.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight; font.weight: Font.DemiBold; font.pixelSize: 11 }
                                                        ToolButton { text: "↑"; implicitWidth: 26; enabled: appliedEffect.index > 0; ToolTip.text: "Move effect up"; onClicked: timelineController.moveEffect(0,timelineController.inspector.itemId,appliedEffect.modelData.id,-1) }
                                                        ToolButton { text: "↓"; implicitWidth: 26; enabled: appliedEffect.index < (timelineController.inspector.effects||[]).length-1; ToolTip.text: "Move effect down"; onClicked: timelineController.moveEffect(0,timelineController.inspector.itemId,appliedEffect.modelData.id,1) }
                                                        ToolButton { text: "⋮"; implicitWidth: 26; onClicked: appliedEffectMenu.popup() }
                                                    }
                                                    HoverHandler { id: effectHeaderHover }
                                                    TapHandler { onDoubleTapped: appliedEffect.expanded = !appliedEffect.expanded }
                                                }
                                                ColumnLayout {
                                                    visible: appliedEffect.expanded
                                                    Layout.fillWidth: true
                                                    Layout.leftMargin: 7
                                                    Layout.rightMargin: 7
                                                    Layout.bottomMargin: 5
                                                    spacing: 1
                                                    Repeater {
                                                        model: appliedEffect.modelData.parameters
                                                        ColumnLayout {
                                                            id: effectParameter
                                                            required property var modelData
                                                            Layout.fillWidth: true
                                                            spacing: 0
                                                            RowLayout {
                                                                Layout.fillWidth: true
                                                                Layout.preferredHeight: 30
                                                                spacing: 4
                                                                Label { text: effectParameter.modelData.name; Layout.preferredWidth: 82; elide: Text.ElideRight; font.pixelSize: 10 }
                                                                Slider { id: effectParameterSlider; from: effectParameter.modelData.minimum; to: effectParameter.modelData.maximum; value: effectParameter.modelData.value; Layout.fillWidth: true; onMoved: timelineController.setEffectParameter(0,timelineController.inspector.itemId,appliedEffect.modelData.id,effectParameter.modelData.name,value) }
                                                                Label { text: Number(effectParameterSlider.value).toFixed(2) + (effectParameter.modelData.unit || ""); Layout.preferredWidth: 52; horizontalAlignment: Text.AlignRight; font.family: "Consolas"; font.pixelSize: 9; color: theme.muted }
                                                                ToolButton { text: "◆"; implicitWidth: 26; implicitHeight: 26; ToolTip.text: "Add keyframe; hold for interpolation"; onClicked: parameterKeyMenu.popup() }
                                                                Menu {
                                                                    id: parameterKeyMenu
                                                                    MenuItem { text: "Add Linear Keyframe"; onTriggered: timelineController.addKeyframe(0,timelineController.inspector.itemId,appliedEffect.modelData.id,effectParameter.modelData.name,timelineController.playheadMs,effectParameterSlider.value,1) }
                                                                    MenuItem { text: "Add Smooth Keyframe"; onTriggered: timelineController.addKeyframe(0,timelineController.inspector.itemId,appliedEffect.modelData.id,effectParameter.modelData.name,timelineController.playheadMs,effectParameterSlider.value,2) }
                                                                    MenuItem { text: "Add Hold Keyframe"; onTriggered: timelineController.addKeyframe(0,timelineController.inspector.itemId,appliedEffect.modelData.id,effectParameter.modelData.name,timelineController.playheadMs,effectParameterSlider.value,0) }
                                                                }
                                                            }
                                                            Repeater {
                                                                model: effectParameter.modelData.keyframes
                                                                RowLayout {
                                                                    required property var modelData
                                                                    Layout.fillWidth: true
                                                                    Layout.preferredHeight: 24
                                                                    Label { text: root.formatTime(modelData.timeMs) + "  " + Number(modelData.value).toFixed(2); font.pixelSize: 9; Layout.fillWidth: true; color: theme.muted }
                                                                    Label { text: ["Hold","Linear","Smooth"][modelData.interpolation]; font.pixelSize: 9; color: theme.faint }
                                                                    ToolButton { text: "×"; implicitWidth: 24; implicitHeight: 22; ToolTip.text: "Remove keyframe"; onClicked: timelineController.removeKeyframe(0,timelineController.inspector.itemId,appliedEffect.modelData.id,effectParameter.modelData.name,modelData.id) }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            Menu {
                                                id: appliedEffectMenu
                                                MenuItem { text: "Reset"; onTriggered: timelineController.resetEffect(0,timelineController.inspector.itemId,appliedEffect.modelData.id) }
                                                MenuSeparator {}
                                                MenuItem { text: "Copy Attributes"; onTriggered: timelineController.copyAttributes() }
                                                MenuItem { text: "Paste Attributes"; onTriggered: timelineController.pasteAttributes(true,true,true,true) }
                                                MenuItem { text: "Save as Preset…"; onTriggered: saveEffectPresetDialog.open() }
                                                MenuSeparator {}
                                                MenuItem { text: "Remove Effect"; onTriggered: timelineController.removeEffect(0,timelineController.inspector.itemId,appliedEffect.modelData.id) }
                                            }
                                        }
                                    }
                                }
                                }
                            }
                        }
                        ColumnLayout { spacing:0
                            WorkspaceHeader { heading:"Audio mixer";description:"Track levels, pan, effects and master limiting";accentColor:theme.green }
                            ScrollView { id: mixerScroll; objectName:"mixerScroll";property real testWheelContentY:contentItem.contentY;clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;contentWidth:mixerRow.implicitWidth+20;contentHeight:mixerRow.height+20
                            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                            WheelHandler{target:null;blocking:true;onWheel:function(event){var d=event.pixelDelta.y!==0?event.pixelDelta.y:event.angleDelta.y/120*48;var f=mixerScroll.contentItem;f.contentY=Math.max(0,Math.min(f.contentHeight-f.height,f.contentY-d));event.accepted=true}}
                            RowLayout { id:mixerRow;width:implicitWidth;height:Math.max(320,root.height-200);x:10;y:10;spacing:8
                                Repeater { model:timelineController.mixerTracks
                                    Rectangle { id:mixerStrip;required property var modelData;Layout.fillHeight:true;Layout.minimumWidth:112;Layout.preferredWidth:112;color:theme.panelRaised;border.color:modelData.trackId==="master"?theme.accent:theme.borderStrong;radius:9
                                        ColumnLayout { anchors.fill:parent;anchors.margins:8;spacing:6
                                            Label{text:modelData.name.toUpperCase();font.bold:true;Layout.alignment:Qt.AlignHCenter;color:modelData.trackId==="master"?theme.accent:theme.text;elide:Text.ElideRight;Layout.maximumWidth:94}
                                            Rectangle{Layout.fillHeight:true;Layout.preferredWidth:16;Layout.alignment:Qt.AlignHCenter;color:theme.canvas;radius:5;border.color:theme.border;Rectangle{anchors.left:parent.left;anchors.right:parent.right;anchors.margins:3;anchors.bottom:parent.bottom;height:Math.max(2,(parent.height-6)*modelData.peak);radius:3;color:modelData.peak>0.95?theme.danger:theme.green}}
                                            Slider{id:gainFader;from:-60;to:12;value:modelData.gainDb;orientation:Qt.Vertical;Layout.preferredHeight:130;onMoved:modelData.trackId==="master"?timelineController.setMasterAudio(value,panKnob.value):timelineController.setTrackAudio(modelData.trackId,value,panKnob.value)}
                                            Slider{id:panKnob;from:-1;to:1;value:modelData.pan;Layout.fillWidth:true;onMoved:modelData.trackId==="master"?timelineController.setMasterAudio(gainFader.value,value):timelineController.setTrackAudio(modelData.trackId,gainFader.value,value)}
                                            ToolButton{text:"◆";ToolTip.text:"Add gain envelope keyframe";onClicked:timelineController.addAudioKeyframe(modelData.trackId==="master"?2:1,modelData.trackId,"gain",timelineController.playheadMs,gainFader.value,1)}
                                            ComboBox{id:mixerFx;width:66;textRole:"name";valueRole:"type";model:timelineController.availableEffects}
                                            ToolButton{text:"+FX";onClicked:timelineController.addEffect(modelData.trackId==="master"?2:1,modelData.trackId,mixerFx.currentValue)}
                                            Repeater{model:modelData.effects;ColumnLayout{id:mixerEffect;required property var modelData;Layout.fillWidth:true
                                                RowLayout{CheckBox{text:mixerEffect.modelData.name;checked:mixerEffect.modelData.enabled;font.pixelSize:9;onToggled:timelineController.bypassEffect(mixerStrip.modelData.trackId==="master"?2:1,mixerStrip.modelData.trackId,mixerEffect.modelData.id,!checked)}ToolButton{text:"X";onClicked:timelineController.removeEffect(mixerStrip.modelData.trackId==="master"?2:1,mixerStrip.modelData.trackId,mixerEffect.modelData.id)}}
                                                Repeater{model:mixerEffect.modelData.parameters;Slider{required property var modelData;from:modelData.minimum;to:modelData.maximum;value:modelData.value;Layout.fillWidth:true;onMoved:timelineController.setEffectParameter(mixerStrip.modelData.trackId==="master"?2:1,mixerStrip.modelData.trackId,mixerEffect.modelData.id,modelData.name,value)}}
                                            }}
                                            CheckBox{visible:modelData.trackId==="master";text:"Limiter";checked:modelData.limiter===undefined?true:modelData.limiter;onToggled:timelineController.setMasterLimiter(checked)}
                                            Row{visible:modelData.trackId!=="master";ToolButton{text:"M";checkable:true;checked:modelData.muted;onClicked:timelineController.setTrackState(modelData.trackId,"muted",checked)}ToolButton{text:"S";checkable:true;checked:modelData.solo;onClicked:timelineController.setTrackState(modelData.trackId,"solo",checked)}}
                                        }
                                    }
                                }
                            }
                            }
                        }
                        ColumnLayout { spacing:0
                            YtpToolbox { objectName:"ytpToolbox";Layout.fillWidth:true;Layout.fillHeight:true;controller:timelineController;appTheme:root.appTheme }
                            WorkspaceHeader { visible:false;heading:"YTP tools";description:"Repeat, reverse, distort and randomize" }
                            GridLayout { visible:false;Layout.fillWidth:true;Layout.leftMargin:10;Layout.rightMargin:10;Layout.topMargin:8;columns:2;columnSpacing:6;rowSpacing:6
                                YtpActionButton { appTheme:root.appTheme;text:"Reverse";description:"Reverse the selected linked clip";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.setReverse(!(timelineController.inspector.reverse||false)) }
                                YtpActionButton { appTheme:root.appTheme;text:"Stutter";description:"Create a rapid alternating stutter";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildStutter(4,80,true) }
                                YtpActionButton { appTheme:root.appTheme;text:"Repeat";description:"Repeat the selected frame sequence";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildFrameRepeat(2,4) }
                                YtpActionButton { appTheme:root.appTheme;text:"Shake";description:"Add a screen-shake effect";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.addEffect(0,timelineController.inspector.itemId,"screen_shake") }
                            }
                            SectionPicker { visible:false;id:ytpSection;objectName:"ytpSection";model:["Stutter","Rapid Reverse","Frame Repeat","Rhythm","Speed","Audio Destruction","FX Packs","Sentence Mixer","Randomizer","Macros"];onCurrentIndexChanged:ytpToolsPages.contentY=0 }
                            Flickable { visible:false;id:ytpToolsPages;objectName:"legacyYtpToolsPages";clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;contentWidth:width;contentHeight:Math.max(height,ytpToolsColumn.implicitHeight);boundsBehavior:Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded;interactive:true}
                                WheelHandler{target:null;blocking:true;onWheel:function(event){var d=event.pixelDelta.y!==0?event.pixelDelta.y:event.angleDelta.y/120*48;ytpToolsPages.contentY=Math.max(0,Math.min(ytpToolsPages.contentHeight-ytpToolsPages.height,ytpToolsPages.contentY-d));event.accepted=true}}
                                ColumnLayout { id:ytpToolsColumn;width:ytpToolsPages.width;spacing:8
                                Rectangle { Layout.fillWidth:true;Layout.leftMargin:10;Layout.rightMargin:10;Layout.preferredHeight:38;radius:7;color:timelineController.selectedIds.length>0?"#173128":"#312817";border.color:timelineController.selectedIds.length>0?theme.green:theme.amber
                                    Label { anchors.fill:parent;anchors.leftMargin:12;verticalAlignment:Text.AlignVCenter;text:timelineController.selectedIds.length>0?timelineController.selectedIds.length+" event(s) ready":"Select a timeline event first";color:timelineController.selectedIds.length>0?theme.green:theme.amber;font.weight:Font.DemiBold }
                                }
                                InspectorCard { title:"Stutter";visible:ytpSection.currentIndex===0
                                    ColumnLayout { anchors.fill:parent;spacing:12
                                        Label{text:"Repeat a tiny slice. Alternate reverse creates the classic back-and-forth YTP stutter.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:theme.muted}
                                        GridLayout { columns:2;Layout.fillWidth:true;columnSpacing:12
                                            Label{text:"Repeats"} SpinBox{id:stutterRepeats;from:2;to:128;value:6;Layout.fillWidth:true}
                                            Label{text:"Slice length (ms)"} SpinBox{id:stutterSlice;from:1;to:5000;value:90;editable:true;Layout.fillWidth:true}
                                            CheckBox{id:stutterAlt;text:"Alternate reverse";checked:true;Layout.columnSpan:2}
                                        }
                                        AccentButton{text:"Build Stutter";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildStutter(stutterRepeats.value,stutterSlice.value,stutterAlt.checked)}
                                    }
                                }
                                InspectorCard { title:"Rapid Reverse";visible:ytpSection.currentIndex===1
                                    ColumnLayout { anchors.fill:parent;spacing:12
                                        Label{text:"Slice the selection into short segments and alternate their playback direction.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:theme.muted}
                                        GridLayout { columns:2;Layout.fillWidth:true;columnSpacing:12
                                            Label{text:"Segments"} SpinBox{id:reverseSegments;from:2;to:128;value:8;Layout.fillWidth:true}
                                            Label{text:"Slice length (ms)"} SpinBox{id:reverseSlice;from:1;to:5000;value:70;editable:true;Layout.fillWidth:true}
                                        }
                                        AccentButton{text:"Build Rapid Reverse";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildRapidReverse(reverseSegments.value,reverseSlice.value)}
                                    }
                                }
                                InspectorCard { title:"Frame Repeat";visible:ytpSection.currentIndex===2
                                    ColumnLayout { anchors.fill:parent;spacing:12
                                        Label{text:"Hold individual frames for a stepped, deliberately broken-motion look.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:theme.muted}
                                        GridLayout { columns:2;Layout.fillWidth:true;columnSpacing:12
                                            Label{text:"Frames"} SpinBox{id:repeatFrames;from:1;to:120;value:2;Layout.fillWidth:true}
                                            Label{text:"Holds per frame"} SpinBox{id:repeatsPerFrame;from:1;to:120;value:5;Layout.fillWidth:true}
                                        }
                                        AccentButton{text:"Build Frame Repeat";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildFrameRepeat(repeatFrames.value,repeatsPerFrame.value)}
                                    }
                                }
                                InspectorCard { title:"Rhythm Repeat";visible:ytpSection.currentIndex===3
                                    GridLayout { anchors.fill:parent;columns:2
                                        Label{text:"Tempo BPM"} SpinBox{id:rhythmBpm;from:20;to:400;value:120;editable:true}
                                        Label{text:"Beats"} SpinBox{id:rhythmBeats;from:1;to:256;value:8}
                                        Label{text:"Gate ms"} SpinBox{id:rhythmGate;from:1;to:5000;value:100;editable:true}
                                        CheckBox{id:rhythmMarkers;text:"Use timeline markers";Layout.columnSpan:2}
                                        Button{text:"Repeat from playhead";Layout.columnSpan:2;Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildRhythmRepeat(timelineController.playheadMs,rhythmBpm.value,rhythmBeats.value,rhythmGate.value,rhythmMarkers.checked)}
                                    }
                                }
                                InspectorCard { title:"Speed Ladder";visible:ytpSection.currentIndex===4
                                    GridLayout { anchors.fill:parent;columns:2
                                        Label{text:"Steps"} SpinBox{id:ladderSteps;from:2;to:64;value:6}
                                        Label{text:"Start speed %"} SpinBox{id:ladderStart;from:5;to:1600;value:50;editable:true}
                                        Label{text:"End speed %"} SpinBox{id:ladderEnd;from:5;to:1600;value:350;editable:true}
                                        Label{text:"Pitch / step"} SpinBox{id:ladderPitch;from:-24;to:24;value:2;editable:true}
                                        CheckBox{id:ladderPreserve;text:"Preserve pitch";Layout.columnSpan:2}
                                        Button{text:"Build Speed Ladder";Layout.columnSpan:2;Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildSpeedLadder(ladderSteps.value,ladderStart.value/100,ladderEnd.value/100,ladderPitch.value,ladderPreserve.checked)}
                                    }
                                }
                                InspectorCard { title:"Safe Earrape";visible:ytpSection.currentIndex===5
                                    ColumnLayout { anchors.fill:parent;spacing:10
                                        Label{text:"Boost + distortion + compression + hard limiter";wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#aeb5c2"}
                                        Slider{id:earrapeIntensity;from:0;to:1;value:.65;Layout.fillWidth:true}
                                        Button{text:"Apply Safe Earrape";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.applySafeEarrape(earrapeIntensity.value)}
                                        Rectangle { Layout.fillWidth:true;Layout.preferredHeight:1;color:theme.border }
                                        Label { text:"Creative audio packs";color:theme.text;font.weight:Font.DemiBold }
                                        Label { text:"Stacked voice and sound treatments. Selecting video also targets its linked audio.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:theme.muted;font.pixelSize:10 }
                                        GridLayout { columns:2;Layout.fillWidth:true;columnSpacing:6;rowSpacing:6
                                            Repeater { model:timelineController.ytpAudioPresets
                                                YtpActionButton { required property var modelData;appTheme:root.appTheme;text:modelData.name;description:modelData.description;showDescription:true;badge:"AUDIO";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.applyYtpAudioPreset(modelData.id) }
                                            }
                                        }
                                    }
                                }
                                InspectorCard { title:"Creative FX Packs";visible:ytpSection.currentIndex===6
                                    ColumnLayout { anchors.fill:parent;spacing:8
                                        Label { text:"One-click effect stacks built for movement, escalation, and remixing. Dynamic packs animate while the timeline plays.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:theme.muted;font.pixelSize:10 }
                                        GridLayout { columns:2;Layout.fillWidth:true;columnSpacing:6;rowSpacing:6
                                            Repeater { model:timelineController.ytpVisualPresets
                                                YtpActionButton { required property var modelData;appTheme:root.appTheme;text:modelData.name;description:modelData.description;showDescription:true;badge:modelData.temporal?"TRAILS":modelData.dynamic?"LIVE":"LOOK";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.applyYtpVisualPreset(modelData.id) }
                                            }
                                        }
                                    }
                                }
                                InspectorCard { title:"Sentence Mixer v1";visible:ytpSection.currentIndex===7
                                    ColumnLayout { anchors.fill:parent
                                        Label{text:"Place markers inside one phrase. Chunks are numbered 0, 1, 2…";wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#aeb5c2"}
                                        TextField{id:sentenceOrder;placeholderText:"Order, e.g. 2,0,1,1";Layout.fillWidth:true}
                                        Button{text:"Mix Marked Chunks";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.buildSentenceMixer(sentenceOrder.text)}
                                    }
                                }
                                InspectorCard { title:"Seeded Randomizer";visible:ytpSection.currentIndex===8
                                    GridLayout { anchors.fill:parent;columns:4;columnSpacing:8;rowSpacing:8
                                        Label{text:"Seed"} SpinBox{id:randomSeed;Layout.columnSpan:3;Layout.fillWidth:true;from:1;to:2147483647;value:1337;editable:true}
                                        Label{text:"Reverse %"} SpinBox{id:randomReverse;Layout.fillWidth:true;from:0;to:100;value:35}
                                        Label{text:"Effect %"} SpinBox{id:randomEffects;Layout.fillWidth:true;from:0;to:100;value:30}
                                        Label{text:"Min speed"} SpinBox{id:randomMinSpeed;Layout.fillWidth:true;from:5;to:1600;value:50}
                                        Label{text:"Max speed"} SpinBox{id:randomMaxSpeed;Layout.fillWidth:true;from:5;to:1600;value:250}
                                        Label{text:"Min pitch"} SpinBox{id:randomMinPitch;Layout.fillWidth:true;from:-48;to:48;value:-12}
                                        Label{text:"Max pitch"} SpinBox{id:randomMaxPitch;Layout.fillWidth:true;from:-48;to:48;value:12}
                                        CheckBox{id:randomShuffle;text:"Shuffle positions";checked:true;Layout.columnSpan:4}
                                        RowLayout { Layout.columnSpan:4;Layout.fillWidth:true
                                            AccentButton{text:"Preview";Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.previewRandomizer(randomSeed.value,randomReverse.value/100,randomEffects.value/100,randomMinSpeed.value/100,randomMaxSpeed.value/100,randomMinPitch.value,randomMaxPitch.value,randomShuffle.checked)}
                                            Button{text:"Commit";Layout.fillWidth:true;enabled:(timelineController.randomizerPreview.changeCount||0)>0;onClicked:timelineController.commitRandomizer()}
                                            Button{text:"Cancel";Layout.fillWidth:true;enabled:(timelineController.randomizerPreview.changeCount||0)>0;onClicked:timelineController.cancelRandomizer()}
                                        }
                                        Label{text:timelineController.randomizerPreview.summary||"No randomization preview";Layout.columnSpan:4;Layout.fillWidth:true;elide:Text.ElideRight;color:theme.muted}
                                    }
                                }
                                InspectorCard { title:"Macros";visible:ytpSection.currentIndex===9
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Layout.fillWidth:true
                                            Button{text:timelineController.macroRecording?"Recording " + timelineController.recordedMacroSteps + " steps":"Record Macro";onClicked:timelineController.macroRecording?timelineController.cancelMacroRecording():timelineController.startMacroRecording()}
                                            TextField{id:macroName;placeholderText:"Macro name";Layout.fillWidth:true}
                                            Button{text:"Save";enabled:timelineController.macroRecording&&timelineController.recordedMacroSteps>0;onClicked:timelineController.saveRecordedMacro(macroName.text)}
                                        }
                                        Repeater { model:timelineController.ytpMacros
                                            RowLayout { required property string modelData;Layout.fillWidth:true
                                                Label{text:modelData;Layout.fillWidth:true;elide:Text.ElideRight}
                                                Button{text:"Apply";enabled:timelineController.selectedIds.length>0;onClicked:timelineController.applyYtpMacro(modelData)}
                                                Button{text:"X";onClicked:timelineController.removeYtpMacro(modelData)}
                                            }
                                        }
                                    }
                                }
                                Item { Layout.preferredHeight:12 }
                                }
                            }
                        }
                        ColumnLayout { spacing:0
                            WorkspaceHeader { heading:"Finish & organize";description:"Transcription, beats, sequences and display";accentColor:theme.cyan;Accessible.name:"Finish tools" }
                            SectionPicker { id:finishSection;objectName:"finishSection";model:["Transcription","Beat Detection","Sequences & Layers","Display & Accessibility"];onCurrentIndexChanged:finishToolsPages.contentY=0 }
                            Flickable { id:finishToolsPages;objectName:"finishToolsPages";clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;contentWidth:width;contentHeight:Math.max(height,finishToolsColumn.implicitHeight);boundsBehavior:Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded;interactive:true}
                                WheelHandler{target:null;blocking:true;onWheel:function(event){var d=event.pixelDelta.y!==0?event.pixelDelta.y:event.angleDelta.y/120*48;finishToolsPages.contentY=Math.max(0,Math.min(finishToolsPages.contentHeight-finishToolsPages.height,finishToolsPages.contentY-d));event.accepted=true}}
                                ColumnLayout { id:finishToolsColumn;width:finishToolsPages.width;spacing:8
                                InspectorCard { title:"Offline Transcription";visible:finishSection.currentIndex===0
                                    ColumnLayout { anchors.fill:parent
                                        Label{text:"Uses bundled FFmpeg + a local whisper.cpp model. Audio never leaves this computer.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#aeb5c2"}
                                        Label{text:"Language";font.pixelSize:10;font.weight:Font.Bold;color:theme.muted}
                                        ComboBox{id:transcriptLanguage;textRole:"label";valueRole:"code";model:[{label:"Auto detect",code:"auto"},{label:"English",code:"en"},{label:"Spanish",code:"es"},{label:"French",code:"fr"},{label:"German",code:"de"},{label:"Italian",code:"it"},{label:"Japanese",code:"ja"}];Layout.fillWidth:true}
                                        AccentButton{text:projectController.transcribing?"Transcribing…":"Choose Model & Transcribe";enabled:!!projectController.sourceName&&!projectController.transcribing;Layout.fillWidth:true;Accessible.name:"Transcribe current source offline";onClicked:whisperModelDialog.open()}
                                        Label{text:"Search transcripts";font.pixelSize:10;font.weight:Font.Bold;color:theme.muted}
                                        TextField{id:transcriptQuery;placeholderText:"Search every transcript";Layout.fillWidth:true;Accessible.name:"Transcript search";onTextChanged:projectController.searchTranscript(text)}
                                        Label{text:projectController.transcriptResults.length+" timed matches";color:"#67d5ff"}
                                        Repeater { model:projectController.transcriptResults
                                            RowLayout { required property var modelData;Layout.fillWidth:true
                                                Label{text:modelData.text+"  "+root.formatTime(modelData.startMs);Layout.fillWidth:true;elide:Text.ElideRight;ToolTip.text:modelData.mediaName}
                                                Button{text:"Clip";Accessible.name:"Create reusable clip from "+modelData.text;onClicked:projectController.createClipFromTranscript(modelData.mediaId,modelData.startMs,modelData.endMs,modelData.text)}
                                            }
                                        }
                                    }
                                }
                                InspectorCard { title:"Beat / Onset Analysis";visible:finishSection.currentIndex===1
                                    ColumnLayout { anchors.fill:parent
                                        Label{text:"Select an audio or video event. Detection adds cyan markers at transients.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#aeb5c2"}
                                        AccentButton{text:"Detect Beats on Selected Clip";enabled:timelineController.selectedIds.length>0;Layout.fillWidth:true;onClicked:timelineController.detectBeats()}
                                    }
                                }
                                InspectorCard { title:"Sequences & Layers";visible:finishSection.currentIndex===2
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { TextField{id:newSequenceName;placeholderText:"Sequence name";Layout.fillWidth:true} Button{text:"Create";onClicked:timelineController.createSequence(newSequenceName.text)} }
                                        Label{text:"Sequence";font.pixelSize:10;font.weight:Font.Bold;color:theme.muted}
                                        ComboBox{id:sequenceChoice;Layout.fillWidth:true;textRole:"name";valueRole:"id";model:timelineController.sequences}
                                        RowLayout { Layout.fillWidth:true
                                            Button{text:"Open";Layout.fillWidth:true;enabled:sequenceChoice.currentValue;onClicked:timelineController.switchSequence(sequenceChoice.currentValue)}
                                            Button{text:"Nest";Layout.fillWidth:true;enabled:sequenceChoice.currentValue!==timelineController.activeSequenceId&&videoTrackChoice.currentValue;onClicked:timelineController.insertNestedSequence(sequenceChoice.currentValue,videoTrackChoice.currentValue,timelineController.playheadMs)}
                                            Button{text:"Remove";enabled:sequenceChoice.currentValue;onClicked:timelineController.removeSequence(sequenceChoice.currentValue)}
                                        }
                                        Label{text:"Target video track";font.pixelSize:10;font.weight:Font.Bold;color:theme.muted}
                                        ComboBox{id:videoTrackChoice;Layout.fillWidth:true;textRole:"name";valueRole:"trackId";model:timelineController.tracks.filter(function(t){return t.kind===0})}
                                        RowLayout { SpinBox{id:adjustDuration;from:40;to:3600000;value:5000;editable:true} Button{text:"Add Adjustment Clip";enabled:videoTrackChoice.currentValue;onClicked:timelineController.createAdjustmentClip(videoTrackChoice.currentValue,timelineController.playheadMs,adjustDuration.value)} }
                                    }
                                }
                                InspectorCard { title:"Display & Accessibility";visible:finishSection.currentIndex===3
                                    GridLayout { anchors.fill:parent;columns:2
                                        Label{text:"UI scale %"} SpinBox{from:75;to:200;value:uiSettings.uiScale*100;onValueModified:uiSettings.uiScale=value/100}
                                        CheckBox{text:"High contrast";checked:uiSettings.highContrast;onToggled:uiSettings.highContrast=checked}
                                        CheckBox{text:"Reduce motion";checked:uiSettings.reducedMotion;onToggled:uiSettings.reducedMotion=checked}
                                        Label{text:"Monitor"} ComboBox{id:monitorChoice;model:Qt.application.screens.map(function(s){return s.name});currentIndex:Math.min(uiSettings.monitorIndex,count-1);onActivated:uiSettings.monitorIndex=currentIndex}
                                        Button{text:externalMonitor.visible?"Close External Monitor":"Open External Monitor";Layout.columnSpan:2;Layout.fillWidth:true;onClicked:externalMonitor.visible?externalMonitor.close():externalMonitor.showFullScreen()}
                                    }
                                }
                                Item{Layout.preferredHeight:12}
                                }
                            }
                        }
                        ColumnLayout { spacing:0
                            WorkspaceHeader { heading:"Remix lab";description:"Sentence construction, beats, compounds and automation";accentColor:"#ff8f5c";Accessible.name:"Remix workspace" }
                            SectionPicker { id:remixSection;objectName:"remixSection";model:["Sentence Mixer","Beat-aware Editing","Compound Clips","Macro Automation","Advanced Effects","Tasks & Cache","Recovery & Collection"];onCurrentIndexChanged:remixToolsPages.contentY=0 }
                            Flickable { id:remixToolsPages;objectName:"remixToolsPages";clip:true;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;contentWidth:width;contentHeight:Math.max(height,remixToolsColumn.implicitHeight);boundsBehavior:Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar{policy:ScrollBar.AsNeeded;interactive:true}
                                WheelHandler{target:null;blocking:true;onWheel:function(event){var d=event.pixelDelta.y!==0?event.pixelDelta.y:event.angleDelta.y/120*48;remixToolsPages.contentY=Math.max(0,Math.min(remixToolsPages.contentHeight-remixToolsPages.height,remixToolsPages.contentY-d));event.accepted=true}}
                                ColumnLayout { id:remixToolsColumn;width:remixToolsPages.width;spacing:8
                                InspectorCard { title:"Sentence Mixer v2";visible:remixSection.currentIndex===0
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { TextField{id:wordQuery;placeholderText:"Find an exact or similar-sounding word";Layout.fillWidth:true;onTextChanged:projectController.searchWords(text,phoneticSearch.checked)} CheckBox{id:phoneticSearch;text:"Phonetic";checked:true;onToggled:projectController.searchWords(wordQuery.text,checked)} }
                                        Label{text:projectController.wordResults.length+" source words found";color:"#67d5ff"}
                                        Repeater { model:projectController.wordResults
                                            RowLayout { required property var modelData;Layout.fillWidth:true
                                                Label{text:modelData.text+(modelData.phonetic?" ≈":"")+" — "+modelData.mediaName;Layout.fillWidth:true;elide:Text.ElideRight}
                                                Button{text:"Add";onClicked:root.sentenceWords=root.sentenceWords.concat([modelData])}
                                            }
                                        }
                                        Label{text:"Sentence: "+root.sentenceWords.map(function(w){return w.text}).join(" ");wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#ffca70"}
                                        Repeater { model:root.sentenceWords
                                            RowLayout { required property var modelData;required property int index;Label{text:(index+1)+". "+modelData.text;Layout.fillWidth:true}Button{text:"X";onClicked:root.sentenceWords=root.sentenceWords.filter(function(_,i){return i!==index})} }
                                        }
                                        TextField{id:sentenceV2Name;placeholderText:"Reusable sentence name";text:"Constructed sentence";Layout.fillWidth:true}
                                         GridLayout { columns:2;Layout.fillWidth:true
                                             Label{text:"Padding ms"} SpinBox{id:wordPadding;from:0;to:1000;value:40}
                                             Label{text:"Crossfade ms"} SpinBox{id:wordCrossfade;from:0;to:500;value:15}
                                         }
                                         Label{text:"Word captions are embedded in the reusable sentence clips and remain editable in Inspector.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#aeb5c2"}
                                        ComboBox{id:remixVideoTrack;Layout.fillWidth:true;textRole:"name";valueRole:"trackId";model:timelineController.tracks.filter(function(t){return t.kind===0})}
                                        RowLayout { AccentButton{text:"Build at Playhead";enabled:root.sentenceWords.length>0&&remixVideoTrack.currentValue;Layout.fillWidth:true;onClicked:if(timelineController.buildSentenceV2(root.sentenceWords,sentenceV2Name.text,wordPadding.value,wordCrossfade.value,remixVideoTrack.currentValue,timelineController.playheadMs))root.sentenceWords=[]} Button{text:"Clear";onClicked:root.sentenceWords=[]} }
                                    }
                                }
                                InspectorCard { title:"Beat-aware Editing";visible:remixSection.currentIndex===1
                                    GridLayout { anchors.fill:parent;columns:2
                                        Label{text:"BPM"} SpinBox{id:gridBpm;from:20;to:400;value:timelineController.beatGrid.bpm||120;editable:true}
                                        Label{text:"Division"} ComboBox{id:gridDivision;model:[1,2,4,8,16,32];currentIndex:2}
                                        Label{text:"Offset ms"} SpinBox{id:gridOffset;from:0;to:3600000;value:timelineController.beatGrid.offsetMs||0;editable:true}
                                        Button{text:"Set Grid";onClicked:timelineController.configureBeatGrid(gridBpm.value,gridOffset.value,gridDivision.currentValue,true)}
                                        Button{text:"Estimate from Selected";Layout.columnSpan:2;Layout.fillWidth:true;enabled:timelineController.selectedIds.length>0;onClicked:timelineController.estimateBeatGrid(gridDivision.currentValue)}
                                        Button{text:"Snap Selection";enabled:timelineController.beatGrid.enabled;onClicked:timelineController.applyBeatTool(0)}
                                        Button{text:"Cut on Beats";enabled:timelineController.beatGrid.enabled;onClicked:timelineController.applyBeatTool(1)}
                                        Button{text:"Audio-reactive Shake";Layout.columnSpan:2;Layout.fillWidth:true;enabled:timelineController.beatGrid.enabled;onClicked:timelineController.applyBeatTool(2,"screen_shake","amount")}
                                    }
                                }
                                InspectorCard { title:"Reusable Compound Clips";visible:remixSection.currentIndex===2
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { TextField{id:compoundName;placeholderText:"Compound name";Layout.fillWidth:true} Button{text:"Compound Selection";enabled:timelineController.selectedIds.length>0;onClicked:timelineController.createCompoundFromSelection(compoundName.text)} }
                                        Label{text:"Saved compound";font.pixelSize:10;font.weight:Font.Bold;color:theme.muted}
                                        ComboBox{id:compoundChoice;Layout.fillWidth:true;textRole:"name";valueRole:"id";model:timelineController.compoundClips}
                                        RowLayout { Layout.fillWidth:true
                                            Button{text:"Insert Live";Layout.fillWidth:true;enabled:!!compoundChoice.currentValue&&!!remixVideoTrack.currentValue;onClicked:timelineController.insertCompound(compoundChoice.currentValue,remixVideoTrack.currentValue,timelineController.playheadMs,false)}
                                            Button{text:"Insert Copy";Layout.fillWidth:true;enabled:!!compoundChoice.currentValue&&!!remixVideoTrack.currentValue;onClicked:timelineController.insertCompound(compoundChoice.currentValue,remixVideoTrack.currentValue,timelineController.playheadMs,true)}
                                            Button{text:"Remove";enabled:!!compoundChoice.currentValue;onClicked:timelineController.removeCompound(compoundChoice.currentValue)}
                                        }
                                    }
                                }
                                InspectorCard { title:"Visual Macro Editor & Batch Automation";visible:remixSection.currentIndex===3
                                    GridLayout { anchors.fill:parent;columns:2;columnSpacing:8;rowSpacing:7
                                        Label{text:"Macro pack"} Label{text:"Add operation"}
                                        RowLayout { ComboBox{id:macroLoad;model:timelineController.ytpMacros;Layout.fillWidth:true} Button{text:"Load";onClicked:timelineController.loadMacroEditor(macroLoad.currentText)} }
                                        RowLayout { ComboBox{id:macroTool;model:["Stutter","Rapid Reverse","Frame Repeat","Rhythm Repeat","Speed Ladder","Safe Earrape","Visual Preset","Sentence Mixer","Audio Preset"];Layout.fillWidth:true} Button{text:"+ Step";onClicked:timelineController.addMacroEditorStep(macroTool.currentIndex)} }
                                        ComboBox{id:macroStepChoice;Layout.columnSpan:2;Layout.fillWidth:true;textRole:"name";model:timelineController.macroEditorSteps;visible:count>0}
                                        RowLayout { Layout.columnSpan:2;Layout.fillWidth:true;visible:macroStepChoice.count>0
                                            Button{text:"Move Up";Layout.fillWidth:true;onClicked:timelineController.moveMacroEditorStep(macroStepChoice.currentIndex,-1)}
                                            Button{text:"Move Down";Layout.fillWidth:true;onClicked:timelineController.moveMacroEditorStep(macroStepChoice.currentIndex,1)}
                                            Button{text:"Remove";onClicked:timelineController.removeMacroEditorStep(macroStepChoice.currentIndex)}
                                        }
                                        TextField{id:visualMacroName;placeholderText:"Macro pack name";Layout.fillWidth:true} Button{text:"Save Pack";onClicked:timelineController.saveVisualMacro(visualMacroName.text)}
                                        Label{text:"Apply scope"} ComboBox{id:macroScope;Layout.fillWidth:true;model:["Selection","Every Cut","Markers / Beats","Transcript Events"]}
                                        Label{text:"Probability %"} SpinBox{id:macroProbability;Layout.fillWidth:true;from:1;to:100;value:100}
                                        Label{text:"Random seed"} SpinBox{id:macroScopeSeed;Layout.fillWidth:true;from:1;to:2147483647;value:1337}
                                        AccentButton{text:"Apply Macro";Layout.fillWidth:true;onClicked:timelineController.applyMacroScope(macroLoad.currentText,macroScope.currentIndex,macroProbability.value/100,macroScopeSeed.value)}
                                        RowLayout { Layout.fillWidth:true;SpinBox{id:variationCount;from:1;to:12;value:4} Button{text:"Preview";onClicked:timelineController.previewMacroVariations(macroLoad.currentText,variationCount.value,macroScopeSeed.value)} }
                                        Label{Layout.columnSpan:2;Layout.fillWidth:true;text:(timelineController.macroVariationPreviews||[]).join("  •  ");elide:Text.ElideRight;color:theme.muted;visible:text!==""}
                                    }
                                }
                                InspectorCard { title:"Advanced YTP Effects";visible:remixSection.currentIndex===4
                                    ColumnLayout { anchors.fill:parent
                                        Label{text:"RGB separation, chromatic aberration, wave/lens warp, kaleidoscope, edge echo, trails, time smear, frame blend, shake, chroma key, and datamosh style are now available in Inspector.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#aeb5c2"}
                                        Button{text:"Open Inspector for Selected Clip";enabled:timelineController.selectedIds.length>0;onClicked:inspectorTabs.currentIndex=1}
                                    }
                                }
                                InspectorCard { title:"Background Tasks & Cache";visible:remixSection.currentIndex===5
                                    ColumnLayout { anchors.fill:parent
                                        Label{text:timelineController.cacheStats.files+" cached files — "+Number(timelineController.cacheStats.megabytes).toFixed(1)+" MB";color:"#67d5ff"}
                                        Repeater { model:timelineController.backgroundTasks
                                            ColumnLayout { required property var modelData;Layout.fillWidth:true
                                                RowLayout { Label{text:modelData.name+": "+modelData.status;Layout.fillWidth:true;elide:Text.ElideRight} Button{text:"Cancel";visible:modelData.canCancel;onClicked:timelineController.cancelBackgroundTask(modelData.id)} }
                                                ProgressBar{from:0;to:1;value:modelData.progress;Layout.fillWidth:true}
                                            }
                                        }
                                        RowLayout { Button{text:"Rebuild Continuous Cache";onClicked:{timelineController.setContinuousCaching(true);timelineController.renderContinuousProgramCache()}} Button{text:"Clear Cache";onClicked:timelineController.clearMediaCache()} }
                                    }
                                }
                                InspectorCard { title:"Recovery & Project Collection";visible:remixSection.currentIndex===6
                                    ColumnLayout { anchors.fill:parent
                                        Label{text:projectController.journalAvailable?"A newer edit journal is available.":"Every dirty edit is journaled after 750 ms.";color:projectController.journalAvailable?"#ffca70":"#7fd6a6";wrapMode:Text.Wrap;Layout.fillWidth:true}
                                        RowLayout { Button{text:"Recover Journal";enabled:projectController.journalAvailable;onClicked:projectController.recoverJournal()} Button{text:"Discard";enabled:projectController.journalAvailable;onClicked:projectController.discardJournal()} }
                                        Button{text:"Collect Project + Media + Proxies";Layout.fillWidth:true;onClicked:archiveDialog.open()}
                                    }
                                }
                                Item{Layout.preferredHeight:12}
                                }
                            }
                        }
                        ColumnLayout {
                            objectName: "effectsBrowser"
                            spacing: 0
                            WorkspaceHeader { heading: "Effects Library"; description: "Build a look or sound without leaving the timeline"; accentColor: theme.cyan }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 8
                                Layout.rightMargin: 8
                                Layout.topMargin: 8
                                Layout.bottomMargin: 5
                                Layout.preferredHeight: 42
                                radius: 6
                                color: theme.surfaceSunken
                                border.color: effectsSearch.activeFocus ? theme.cyan : theme.border
                                border.width: 1
                                Label { anchors.left: parent.left; anchors.leftMargin: 11; anchors.verticalCenter: parent.verticalCenter; text: "⌕"; color: effectsSearch.activeFocus ? theme.cyan : theme.faint; font.pixelSize: 19 }
                                TextField {
                                    id: effectsSearch
                                    objectName: "effectsSearch"
                                    anchors.left: parent.left
                                    anchors.leftMargin: 34
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 34
                                    placeholderText: "Search names, styles, or jobs"
                                    selectByMouse: true
                                    background: Item {}
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 8
                                Layout.rightMargin: 8
                                Layout.bottomMargin: 6
                                spacing: 4
                                Repeater {
                                    model: ["All", "Video", "Audio", "YTP", "Favorites"]
                                    ToolButton {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        implicitHeight: 28
                                        text: modelData === "Favorites" ? "★" : modelData
                                        checkable: true
                                        checked: root.effectBrowserFilter === modelData
                                        ToolTip.visible: hovered && modelData === "Favorites"
                                        ToolTip.text: "Favorites"
                                        contentItem: Label { text: parent.text; color: parent.checked ? theme.text : theme.muted; font.pixelSize: 10; font.weight: parent.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { radius: 5; color: parent.checked ? "#31324c" : (parent.hovered ? theme.panelHover : theme.panelRaised); border.color: parent.checked ? theme.accent : theme.border; border.width: 1 }
                                        onClicked: root.effectBrowserFilter = modelData
                                    }
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 8
                                Layout.rightMargin: 8
                                Layout.bottomMargin: 7
                                Layout.preferredHeight: 38
                                radius: 5
                                color: timelineController.selectedIds.length > 0 ? "#12262a" : theme.panelRaised
                                border.color: timelineController.selectedIds.length > 0 ? "#286c6f" : theme.border
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8; spacing: 6
                                    Rectangle { width: 7; height: 7; radius: 4; color: timelineController.selectedIds.length > 0 ? theme.green : theme.faint }
                                    Label { Layout.fillWidth: true; text: timelineController.selectedIds.length > 0 ? (timelineController.selectedIds.length + " event" + (timelineController.selectedIds.length > 1 ? "s" : "") + " selected") : "Select a timeline event to apply"; color: timelineController.selectedIds.length > 0 ? theme.text : theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                                    Label { visible: timelineController.selectedIds.length > 0; text: ((timelineController.inspector.effects || []).length) + " applied"; color: theme.green; font.pixelSize: 9; font.weight: Font.DemiBold }
                                }
                            }
                            ListView {
                                id: effectsBrowserList
                                objectName: "effectsBrowserList"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: 0
                                clip: true
                                spacing: 1
                                model: root.effectBrowserEntries(effectsSearch.text, root.effectBrowserFilter)
                                section.property: "category"
                                section.criteria: ViewSection.FullString
                                section.delegate: Rectangle {
                                    required property string section
                                    width: effectsBrowserList.width
                                    height: 30
                                    color: theme.surfaceSunken
                                    Rectangle { anchors.left: parent.left; anchors.leftMargin: 9; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 13; radius: 2; color: section.startsWith("Audio") ? theme.amber : section === "Presets" ? theme.accent : theme.cyan }
                                    Label { anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter; text: section.toUpperCase(); color: theme.muted; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: .7 }
                                }
                                delegate: Rectangle {
                                    id: effectBrowserItem
                                    objectName: "effectBrowserItem"
                                    required property var modelData
                                    width: effectsBrowserList.width
                                    height: 62
                                    radius: 5
                                    color: effectBrowserHover.containsMouse ? theme.panelHover : "transparent"
                                    border.color: effectBrowserHover.containsMouse ? theme.borderStrong : "transparent"
                                    border.width: 1
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 5
                                        spacing: 8
                                        Rectangle {
                                            Layout.preferredWidth: 38; Layout.preferredHeight: 38; radius: 7
                                            color: effectBrowserItem.modelData.preset ? "#392f5f" : effectBrowserItem.modelData.audio ? "#3a2c20" : effectBrowserItem.modelData.ytp ? "#17394a" : "#242b38"
                                            border.color: effectBrowserItem.modelData.preset ? theme.accent : effectBrowserItem.modelData.audio ? theme.amber : effectBrowserItem.modelData.ytp ? theme.cyan : theme.borderStrong
                                            Label { anchors.centerIn: parent; text: effectBrowserItem.modelData.preset ? "◇" : (effectBrowserItem.modelData.audio ? "♪" : effectBrowserItem.modelData.ytp ? "⚡" : "FX"); color: effectBrowserItem.modelData.preset ? theme.accentBright : effectBrowserItem.modelData.audio ? theme.amber : effectBrowserItem.modelData.ytp ? theme.cyan : theme.muted; font.pixelSize: effectBrowserItem.modelData.ytp ? 16 : 11; font.weight: Font.Bold }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 1
                                            RowLayout { Layout.fillWidth: true; spacing: 5
                                                Label { text: effectBrowserItem.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight; color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                                                Label { visible: effectBrowserItem.modelData.heavy; text: "TEMPORAL"; color: theme.amber; font.pixelSize: 7; font.weight: Font.Bold; leftPadding: 4; rightPadding: 4; background: Rectangle { radius: 3; color: "#33291b"; border.color: "#684e28" } }
                                                Label { visible: effectBrowserItem.modelData.ytp && !effectBrowserItem.modelData.heavy; text: "YTP"; color: theme.cyan; font.pixelSize: 7; font.weight: Font.Bold }
                                            }
                                            Label { Layout.fillWidth: true; text: effectBrowserItem.modelData.description || "Effect"; elide: Text.ElideRight; color: theme.muted; font.pixelSize: 9 }
                                        }
                                        ToolButton {
                                            id: effectFavoriteButton
                                            visible: !effectBrowserItem.modelData.preset
                                            text: effectBrowserItem.modelData.favorite ? "★" : "☆"
                                            implicitWidth: 26; implicitHeight: 30
                                            contentItem: Label { text: effectFavoriteButton.text; color: effectBrowserItem.modelData.favorite ? theme.amber : theme.faint; font.pixelSize: 15; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                            ToolTip.visible: hovered
                                            ToolTip.text: effectBrowserItem.modelData.favorite ? "Remove favorite" : "Add favorite"
                                            onClicked: timelineController.toggleEffectFavorite(effectBrowserItem.modelData.type)
                                        }
                                        ToolButton { text: "+"; implicitWidth: 30; implicitHeight: 30; enabled: timelineController.selectedIds.length > 0; ToolTip.visible: hovered; ToolTip.text: effectBrowserItem.modelData.preset ? "Apply preset" : "Add to selected event"; font.pixelSize: 17; onClicked: root.applyEffectBrowserEntry(effectBrowserItem.modelData) }
                                    }
                                    HoverHandler { id: effectBrowserHover }
                                    TapHandler { acceptedButtons: Qt.LeftButton; onDoubleTapped: root.applyEffectBrowserEntry(effectBrowserItem.modelData) }
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: effectsBrowserList.count === 0
                                    text: root.effectBrowserFilter === "Favorites" ? "Favorite effects with the ☆ button" : "No effects match this search"
                                    color: theme.faint
                                    font.pixelSize: 10
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: 9; Layout.rightMargin: 9; Layout.topMargin: 5; Layout.bottomMargin: 7
                                text: "Double-click to apply  •  ☆ saves a favorite"
                                color: theme.faint
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }
        }

        Panel {
            id: timelinePanel
            objectName: "timelinePanel"
            SplitView.preferredHeight: Math.round(root.height * 0.44)
            SplitView.minimumHeight: root.height < 720 ? 230 : 300
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    objectName: "timelineToolbar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    Layout.minimumHeight: 34
                    Layout.maximumHeight: 34
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    spacing: 2
                    AppToolButton { text: "↖"; implicitWidth:30; checked:true; helpText:"Selection tool (V)" }
                        AppToolButton { objectName:"timelineSplitButton"; text: "✂"; implicitWidth:30; helpText:"Blade: split selected clips at the playhead (S)"; enabled: timelineController.durationMs > 0; onClicked: root.splitTimelineAtPlayhead() }
                        AppToolButton { objectName:"timelineDeleteButton"; text: "Delete"; helpText:"Remove selected clips using the current ripple mode"; enabled: timelineController.selectedIds.length > 0; onClicked: timelineController.deleteSelected() }
                        AppToolButton { objectName:"timelineDuplicateButton"; text: "Duplicate"; helpText:"Duplicate selected clips immediately after the selection (D)"; enabled: timelineController.selectedIds.length > 0; onClicked: timelineController.duplicateSelected() }
                        AppToolButton { objectName:"timelineGroupButton"; text: "Group"; helpText:"Group selected clips so later selection and moves keep them together"; enabled: timelineController.selectedIds.length > 1; onClicked: timelineController.groupSelected() }
                        AppToolButton { objectName:"timelineMarkerButton"; text: "+M"; implicitWidth:34; helpText:"Add marker at playhead (M)"; onClicked: timelineController.addMarker(timelineController.playheadMs, "Marker") }
                        AppToolButton { objectName:"timelineMagnetButton"; text: "⌁"; implicitWidth:30; font.pixelSize:18; helpText:"Snapping: cuts, markers and playhead (N)"; checked: timelineController.snapping; checkable: true; onToggled: timelineController.snapping = checked }
                    Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: theme.border; Layout.leftMargin: 2; Layout.rightMargin: 2 }
                    Label { visible:false;text: "RIPPLE"; color: theme.faint; font.pixelSize: 9 }
                    ComboBox {
                        model: ["Ripple Off", "Ripple Tracks", "Ripple All"]
                        currentIndex: timelineController.rippleMode
                        onActivated: timelineController.rippleMode = currentIndex
                        Layout.preferredWidth: 96
                        implicitHeight: 30
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: theme.border; Layout.leftMargin: 2; Layout.rightMargin: 2 }
                        AppToolButton { visible:false;text: "Reverse"; helpText:"Toggle reverse playback for the selected clip"; enabled: timelineController.selectedIds.length > 0; onClicked: timelineController.setReverse(!(timelineController.inspector.reverse||false)) }
                        AppToolButton { visible:false;text: "Stutter"; helpText:"Build a 4× 80 ms alternating-reverse stutter"; enabled: timelineController.selectedIds.length > 0; onClicked: timelineController.buildStutter(4,80,true) }
                        AppToolButton { visible:false;text: "Shake"; helpText:"Add a screen-shake effect to the selected clip"; enabled: timelineController.selectedIds.length > 0; onClicked: timelineController.addEffect(0,timelineController.inspector.itemId,"screen_shake") }
                    Item { Layout.fillWidth: true }
                    Pill { text: root.formatTime(timelineController.playheadMs); font.family: "Consolas"; color: theme.cyan }
                    AppToolButton { objectName:"timelineZoomOutButton"; text: "−"; helpText:"Zoom out (Ctrl+-)"; onClicked: root.zoomTimelineAt(0.8, timelineFlick.width / 2) }
                    Slider {
                        id: timelineZoomSlider
                        objectName: "timelineZoomSlider"
                        from: Math.log(5); to: Math.log(4000)
                        value: Math.log(timelineController.pixelsPerSecond)
                        Layout.preferredWidth: 100
                        onMoved: root.zoomTimelineAt(Math.exp(value) / timelineController.pixelsPerSecond, timelineFlick.width / 2)
                    }
                    AppToolButton { objectName:"timelineZoomInButton"; text: "+"; helpText:"Zoom in (Ctrl++)"; onClicked: root.zoomTimelineAt(1.25, timelineFlick.width / 2) }
                    AppToolButton { objectName:"timelineZoomFitButton"; text: "⤢"; implicitWidth:30; helpText:"Fit the full edit in the timeline"; onClicked: root.fitTimelineToWindow() }
                }
                Flickable {
                    id: timelineFlick
                    objectName: "timelineFlick"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    interactive: false
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: root.timelineContentWidth
                    contentHeight: timelineColumn.height
                    ScrollBar.horizontal: ScrollBar {
                        id: timelineHorizontalScrollBar
                        objectName: "timelineHorizontalScrollBar"
                        policy: ScrollBar.AlwaysOn
                        interactive: true
                        height: 14
                        onPressedChanged: {
                            if (pressed) {
                                timelineFollowResume.stop()
                                root.timelineFollowSuppressed = true
                            } else {
                                timelineFollowResume.restart()
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; interactive: true }
                    property real middlePanStartX: 0
                    DragHandler {
                        id: timelineMiddlePan
                        objectName: "timelineMiddlePan"
                        target: null
                        acceptedDevices: PointerDevice.Mouse
                        acceptedButtons: Qt.MiddleButton
                        xAxis.enabled: true
                        yAxis.enabled: false
                        cursorShape: active ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        onActiveChanged: {
                            if (active) {
                                root.suspendTimelineFollow()
                                timelineFlick.middlePanStartX = timelineFlick.contentX
                            } else if (root.timelineFollowSuppressed) {
                                timelineFollowResume.restart()
                            }
                        }
                        onActiveTranslationChanged: {
                            if (active) {
                                root.suspendTimelineFollow()
                                timelineFlick.contentX = Math.max(0, Math.min(
                                    timelineFlick.contentWidth - timelineFlick.width,
                                    timelineFlick.middlePanStartX - activeTranslation.x))
                            }
                        }
                    }
                    WheelHandler {
                        target: null
                        onWheel: function(event) {
                            if (event.modifiers & Qt.ControlModifier) {
                                const zoomDelta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
                                root.zoomTimelineAt(zoomDelta >= 0 ? 1.2 : (1 / 1.2), event.x)
                                event.accepted = true
                                return
                            }
                            const verticalDelta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y * 0.5
                            const horizontalDelta = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x * 0.5
                            if ((event.modifiers & Qt.ShiftModifier) || horizontalDelta !== 0)
                                root.suspendTimelineFollow()
                            if ((event.modifiers & Qt.ShiftModifier) || horizontalDelta !== 0)
                                timelineFlick.contentX = Math.max(0, Math.min(timelineFlick.contentWidth - timelineFlick.width,
                                                                             timelineFlick.contentX - (horizontalDelta !== 0 ? horizontalDelta : verticalDelta)))
                            else
                                timelineFlick.contentY = Math.max(0, Math.min(timelineFlick.contentHeight - timelineFlick.height,
                                                                             timelineFlick.contentY - verticalDelta))
                            event.accepted = true
                        }
                    }
                    onContentXChanged: timelineController.setVisibleRange(Math.max(0,(contentX-root.trackHeaderWidth)*1000/timelineController.pixelsPerSecond),Math.max(0,(contentX+width-root.trackHeaderWidth)*1000/timelineController.pixelsPerSecond))
                    onWidthChanged: timelineController.setVisibleRange(Math.max(0,(contentX-root.trackHeaderWidth)*1000/timelineController.pixelsPerSecond),Math.max(0,(contentX+width-root.trackHeaderWidth)*1000/timelineController.pixelsPerSecond))
                    Column {
                        id: timelineColumn
                        width: root.timelineContentWidth
                        Rectangle {
                            id: timelineRuler
                            objectName: "timelineRuler"
                            width: parent.width; height: 34; color: theme.canvas
                            z: 1000
                            transform: Translate { y: timelineFlick.contentY }
                            Rectangle { width: root.trackHeaderWidth; height: parent.height; color: theme.panelRaised; border.color: theme.border }
                            readonly property real majorTickMs: timelineController.pixelsPerSecond >= 1800 ? 100 :
                                timelineController.pixelsPerSecond >= 700 ? 250 :
                                timelineController.pixelsPerSecond >= 260 ? 1000 :
                                timelineController.pixelsPerSecond >= 70 ? 5000 : 10000
                            Row { x: root.trackHeaderWidth
                                Repeater { model: Math.ceil((timelineFlick.contentWidth - root.trackHeaderWidth) /
                                                           (timelineController.pixelsPerSecond * timelineRuler.majorTickMs / 1000))
                                    Item { width: timelineController.pixelsPerSecond * timelineRuler.majorTickMs / 1000; height: 34
                                        Rectangle { x: 0; width: 1; height: parent.height; color: theme.borderStrong }
                                        Label { x: 6; anchors.verticalCenter: parent.verticalCenter; text: root.formatTime(index * timelineRuler.majorTickMs).slice(3); color: theme.muted; font.family: "Consolas"; font.pixelSize: 10 }
                                    }
                                }
                            }
                            TapHandler { onTapped: function(eventPoint) { root.scrubTimelineAt(eventPoint.position.x, true) } }
                        }
                        Repeater {
                            model: timelineController.timelineRows
                            Rectangle {
                                id: trackRow
                                required property var modelData
                                required property int index
                                width: timelineColumn.width; height: modelData.height
                                color: modelData.kind === 0 ? "#0d1119" : "#0c1518"
                                border.color: theme.border
                                Rectangle { width: root.trackHeaderWidth; height: parent.height; color: theme.panelRaised
                                    Rectangle { width: 2; height: parent.height; color: trackRow.modelData.color }
                                }
                                Label { x: 10; anchors.verticalCenter:parent.verticalCenter; text: modelData.name; font.bold: true; color: theme.text }
                                Pill { visible:false;x: 14; y: 34; text: modelData.kind===0?"VIDEO":"AUDIO"; color: modelData.color }
                                Row { x: 40; anchors.verticalCenter:parent.verticalCenter; spacing: 1
                                        AppToolButton { objectName:"trackMuteButton"; visible:trackRow.modelData.kind!==0; width: 24; height: 24; text: "M"; helpText:"Mute audio track"; checkable: true; checked: trackRow.modelData.muted; onClicked: timelineController.setTrackState(trackRow.modelData.trackId,"muted",checked) }
                                        AppToolButton { objectName:"trackSoloButton"; visible:trackRow.modelData.kind!==0; width: 24; height: 24; text: "S"; helpText:"Solo audio track"; checkable: true; checked: trackRow.modelData.solo; onClicked: timelineController.setTrackState(trackRow.modelData.trackId,"solo",checked) }
                                        AppToolButton { objectName:"trackLockButton"; width: 24; height: 24; text: trackRow.modelData.locked ? "▣" : "□"; helpText:"Lock track"; checkable: true; checked: trackRow.modelData.locked; onClicked: timelineController.setTrackState(trackRow.modelData.trackId,"locked",checked) }
                                        AppToolButton { objectName:"trackVisibilityButton"; visible:trackRow.modelData.kind===0; width: 24; height: 24; text: trackRow.modelData.visible ? "◉" : "○"; helpText:"Video track visibility"; onClicked: timelineController.setTrackState(trackRow.modelData.trackId,"visible",!trackRow.modelData.visible) }
                                }
                                Column { x: root.trackHeaderWidth-29; y: 7; spacing: 2; z: 5
                                    AppToolButton { width:22;height:22;text:"↑";helpText:"Move track up";enabled:trackRow.modelData.order>0;onClicked:timelineController.moveTrack(trackRow.modelData.trackId,-1) }
                                    AppToolButton { width:22;height:22;text:"↓";helpText:"Move track down";enabled:trackRow.modelData.order<timelineController.tracks.length-1;onClicked:timelineController.moveTrack(trackRow.modelData.trackId,1) }
                                }
                                DropArea { objectName: "timelineDropArea_" + trackRow.modelData.trackId; anchors.left: parent.left; anchors.leftMargin: root.trackHeaderWidth; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; keys: ["application/x-ytp-library-clip"]
                                    onDropped: function(drop) {
                                        const time = Math.max(0, drop.x * 1000 / timelineController.pixelsPerSecond)
                                        const mimeClipId = drop.getDataAsString("application/x-ytp-library-clip")
                                        const draggedClipId = mimeClipId !== "" ? mimeClipId :
                                            (drop.source && drop.source.clipId ? drop.source.clipId : "")
                                        if (draggedClipId === "")
                                            return
                                        drop.acceptProposedAction()
                                        // Insertion rebuilds the track delegate, so it must be the
                                        // final statement in this handler's QML context.
                                        timelineController.insertClip(draggedClipId, trackRow.modelData.trackId, time, 0)
                                    }
                                    Rectangle { anchors.fill: parent; color: parent.containsDrag ? "#263c50" : "transparent" }
                                }
                                MouseArea {
                                    objectName: "timelineMarqueeArea"
                                    property string timelineTrackId: trackRow.modelData.trackId
                                    anchors.left: parent.left
                                    anchors.leftMargin: root.trackHeaderWidth
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    acceptedButtons: Qt.LeftButton
                                    preventStealing: true
                                    cursorShape: Qt.CrossCursor
                                    onPressed: function(mouse) { root.beginTimelineMarquee(this, mouse.x, mouse.y) }
                                    onPositionChanged: function(mouse) { if (pressed) root.updateTimelineMarquee(this, mouse.x, mouse.y) }
                                    onReleased: function(mouse) { root.finishTimelineMarquee(this, mouse.x, mouse.y) }
                                    onCanceled: root.marqueeActive = false
                                }
                                Repeater { model: trackRow.modelData.items
                                    Rectangle {
                                        id: clipEvent
                                        required property var modelData
                                        objectName: "timelineClip_" + modelData.itemId
                                        // Bind directly to the list value. Merely touching the property and then
                                        // calling isSelected() did not give QML a dependable value dependency, so
                                        // the C++ selection changed on the first click while the highlight stayed stale.
                                        readonly property bool isSelected: timelineController.selectedIds.indexOf(modelData.itemId) !== -1
                                        readonly property bool isRippleFollower: { timelineController.selectedIds; timelineController.rippleMode; return timelineController.isRippleMoveFollower(modelData.itemId) }
                                        visible: modelData.trackId === trackRow.modelData.trackId
                                        z: isSelected ? 4 : 2
                                        x: root.trackHeaderWidth + modelData.startMs * timelineController.pixelsPerSecond / 1000 + (root.timelineDragActive && (isSelected || isRippleFollower) ? root.timelineDragDeltaPx : 0)
                                        y: 5; width: Math.max(3, modelData.durationMs * timelineController.pixelsPerSecond / 1000); height: trackRow.height - 10
                                        radius: 2; color: isSelected ? (modelData.kind===0?"#48617d":"#376d60") : (modelData.adjustment?"#51446a":modelData.kind===0?"#34465b":"#285146"); opacity: trackRow.modelData.locked ? 0.55 : 1
                                        border.width: 1; border.color: isSelected ? theme.accentBright : theme.borderStrong; clip: true
                                        Item {
                                            objectName: "clipMediaPreview"
                                            anchors.fill: parent
                                            anchors.margins: 2
                                            opacity: clipEvent.modelData.kind === 0 ? 0.82 : 0.96
                                            TimelineFilmstrip {
                                                objectName: "timelineFilmstrip"
                                                anchors.fill: parent
                                                visible: clipEvent.modelData.kind === 0
                                                source: clipEvent.modelData.thumbnailUrl
                                                thumbnailProvider: timelineController
                                                cacheGeneration: timelineController.timelineThumbnailGeneration
                                                mediaId: clipEvent.modelData.mediaId
                                                sourceStartMs: clipEvent.modelData.sourceStartMs
                                                sourceEndMs: clipEvent.modelData.sourceEndMs
                                                playbackRate: clipEvent.modelData.speed
                                                reverse: clipEvent.modelData.reverse
                                                freeze: clipEvent.modelData.freeze
                                                timelineStartMs: clipEvent.modelData.startMs
                                                viewportStartMs: Math.max(0, (timelineFlick.contentX - root.trackHeaderWidth) * 1000 / timelineController.pixelsPerSecond)
                                                viewportEndMs: Math.max(0, (timelineFlick.contentX + timelineFlick.width - root.trackHeaderWidth) * 1000 / timelineController.pixelsPerSecond)
                                                pixelsPerSecond: timelineController.pixelsPerSecond
                                            }
                                            Image {
                                                anchors.fill: parent
                                                visible: clipEvent.modelData.kind !== 0
                                                source: clipEvent.modelData.waveformUrl
                                                fillMode: Image.Stretch
                                                sourceSize.width: 8192
                                                sourceSize.height: 128
                                                smooth: false
                                                mipmap: false
                                            }
                                        }
                                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 25; color: "#b8101319" }
                                        Label { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 6; text: root.readableClipName(modelData.name); color: "white"; font.weight: Font.DemiBold; font.pixelSize: 10; elide: Text.ElideRight; width: parent.width-(modelData.linked?30:12) }
                                        Label {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.rightMargin: 6
                                            anchors.topMargin: 4
                                            visible: modelData.linked && (clipEvent.isSelected || clipHover.hovered)
                                            text: "⛓"
                                            color: theme.cyan
                                            font.pixelSize: 11
                                            HoverHandler { id: linkedIndicatorHover }
                                            ToolTip.visible: linkedIndicatorHover.hovered
                                            ToolTip.text: "Linked audio/video"
                                        }
                                        HoverHandler { id: clipHover }
                                        Rectangle {
                                            objectName: "timelineSelectionEmphasis"
                                            visible: clipEvent.isSelected
                                            anchors.fill: parent
                                            anchors.margins: 0
                                            radius: 2
                                            color: "transparent"
                                            border.width: 1
                                            border.color: theme.accentBright
                                        }
                                        Row { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 5; spacing: 4
                                            Pill { visible:modelData.grouped;text:"GROUP";color:theme.amber }
                                        }
                                        Rectangle { width: modelData.fadeInMs * timelineController.pixelsPerSecond / 1000; height: 3; color: "#ffe08a"; anchors.left: parent.left; anchors.bottom: parent.bottom }
                                        Rectangle { width: modelData.fadeOutMs * timelineController.pixelsPerSecond / 1000; height: 3; color: "#ffe08a"; anchors.right: parent.right; anchors.bottom: parent.bottom }
                                        Menu {
                                            id: clipContextMenu
                                            objectName: "clipContextMenu_" + clipEvent.modelData.itemId
                                            MenuItem { text:"Cut"; onTriggered: { timelineController.copySelected(); timelineController.deleteSelected() } }
                                            MenuItem { text:"Copy"; onTriggered: timelineController.copySelected() }
                                            MenuItem { text:"Delete"; onTriggered: timelineController.deleteSelected() }
                                            MenuItem { text:"Duplicate"; onTriggered: timelineController.duplicateSelected() }
                                            MenuItem { text:"Rename…"; onTriggered: root.renameTimelineClip(clipEvent.modelData.itemId, clipEvent.modelData.name) }
                                            MenuSeparator {}
                                            MenuItem { text:"Split at Playhead"; onTriggered: root.splitTimelineAtPlayhead() }
                                            MenuSeparator {}
                                            MenuItem { text:"Reverse"; onTriggered: timelineController.setReverse(!(timelineController.inspector.reverse || false)) }
                                            MenuItem { text:"Freeze Frame"; onTriggered: timelineController.setFreeze(true, timelineController.playheadMs - clipEvent.modelData.startMs) }
                                            Menu { title:"YTP"
                                                MenuItem { text:"Stutter"; onTriggered: timelineController.buildStutter(4,80,true) }
                                                MenuItem { text:"Repeat"; onTriggered: timelineController.buildFrameRepeat(2,4) }
                                                MenuItem { text:"Screen Shake"; onTriggered: timelineController.addEffect(0,timelineController.inspector.itemId,"screen_shake") }
                                            }
                                            MenuSeparator {}
                                            MenuItem { text:"Unlink Audio"; onTriggered: timelineController.unlinkSelected() }
                                            MenuItem { text:"Group"; enabled:timelineController.selectedIds.length>1;onTriggered: timelineController.groupSelected() }
                                            MenuSeparator {}
                                            MenuItem { text:"Properties"; onTriggered: { inspectorTabs.currentIndex=0;root.inspectorContentIndex=1;uiSettings.inspectorCollapsed=false } }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            z: 1
                                            enabled: !trackRow.modelData.locked
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            preventStealing: true
                                            cursorShape: Qt.SizeAllCursor
                                            property real pressTimelineX: 0
                                            property bool wasSelectedOnPress: false
                                            property bool additiveOnPress: false
                                            onPressed: function(mouse) {
                                                if (mouse.button === Qt.RightButton) {
                                                    if (!timelineController.isSelected(clipEvent.modelData.itemId))
                                                        timelineController.select(clipEvent.modelData.itemId, false)
                                                    clipContextMenu.popup()
                                                    return
                                                }
                                                wasSelectedOnPress = timelineController.isSelected(clipEvent.modelData.itemId)
                                                additiveOnPress = (mouse.modifiers & Qt.ControlModifier) !== 0
                                                if (!wasSelectedOnPress)
                                                    timelineController.select(clipEvent.modelData.itemId, additiveOnPress)
                                                pressTimelineX = mapToItem(timelineColumn, mouse.x, mouse.y).x
                                                root.timelineDragDeltaPx = 0
                                                root.timelineDragActive = true
                                            }
                                            onPositionChanged: function(mouse) {
                                                if (pressed && (pressedButtons & Qt.LeftButton)) {
                                                    const rawDeltaPx = mapToItem(timelineColumn, mouse.x, mouse.y).x - pressTimelineX
                                                    const rawStartMs = clipEvent.modelData.startMs + rawDeltaPx * 1000 / timelineController.pixelsPerSecond
                                                    const snappedStartMs = timelineController.snapMove(rawStartMs)
                                                    root.timelineDragDeltaPx = (snappedStartMs - clipEvent.modelData.startMs) * timelineController.pixelsPerSecond / 1000
                                                }
                                            }
                                            onReleased: function(mouse) {
                                                if (mouse.button === Qt.RightButton)
                                                    return
                                                const delta = root.timelineDragDeltaPx
                                                if (Math.abs(delta) < 4) {
                                                    root.timelineDragActive = false
                                                    root.timelineDragDeltaPx = 0
                                                    // Unselected clips were already selected on press so their
                                                    // highlight appears immediately. Only an originally selected
                                                    // clip needs a release-time action (collapse or Ctrl-toggle).
                                                    if (wasSelectedOnPress)
                                                        timelineController.select(clipEvent.modelData.itemId, additiveOnPress)
                                                    return
                                                }
                                                if (!timelineController.isSelected(clipEvent.modelData.itemId))
                                                    timelineController.select(clipEvent.modelData.itemId, false)
                                                const targetStartMs = clipEvent.modelData.startMs + delta * 1000 / timelineController.pixelsPerSecond
                                                root.timelineDragActive = false
                                                root.timelineDragDeltaPx = 0
                                                timelineController.moveSelected(targetStartMs)
                                            }
                                            onCanceled: {
                                                root.timelineDragActive = false
                                                root.timelineDragDeltaPx = 0
                                            }
                                        }
                                        Rectangle { visible:clipEvent.isSelected;z:2; width: 4; height: parent.height; color: theme.accentBright; anchors.left: parent.left; MouseArea { anchors.fill: parent;cursorShape:Qt.SizeHorCursor;onReleased:function(mouse){var p=mapToItem(timelineColumn,mouse.x,mouse.y);timelineController.trimStart(clipEvent.modelData.itemId,Math.max(0,(p.x-root.trackHeaderWidth)*1000/timelineController.pixelsPerSecond))} } }
                                        Rectangle { visible:clipEvent.isSelected;z:2; width: 4; height: parent.height; color: theme.accentBright; anchors.right: parent.right; MouseArea { anchors.fill: parent;cursorShape:Qt.SizeHorCursor;onReleased:function(mouse){var p=mapToItem(timelineColumn,mouse.x,mouse.y);timelineController.trimEnd(clipEvent.modelData.itemId,Math.max(0,(p.x-root.trackHeaderWidth)*1000/timelineController.pixelsPerSecond))} } }
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        objectName: "timelineMarquee"
                        visible: root.marqueeActive
                        x: Math.min(root.marqueeStartX, root.marqueeCurrentX)
                        y: Math.min(root.marqueeStartY, root.marqueeCurrentY)
                        width: Math.max(1, Math.abs(root.marqueeCurrentX - root.marqueeStartX))
                        height: Math.max(1, Math.abs(root.marqueeCurrentY - root.marqueeStartY))
                        color: "#334bd6ff"
                        border.width: 1
                        border.color: theme.cyan
                        z: 1500
                    }
                    Repeater { model: timelineController.markers
                        Rectangle { required property var modelData; x: root.trackHeaderWidth + modelData.timeMs * timelineController.pixelsPerSecond / 1000; y: 0; width: 2; height: timelineFlick.height; color: modelData.color
                            Label { text: modelData.label; color: parent.color; rotation: 90; transformOrigin: Item.TopLeft }
                        }
                    }
                    Rectangle { id:timelinePlayhead;objectName:"timelinePlayhead";x: root.trackHeaderWidth + timelineController.playheadMs * timelineController.pixelsPerSecond / 1000; y: timelineFlick.contentY; width: 2; height: timelineFlick.height; color: theme.accent;z:2000
                        Rectangle { width: 12; height: 12; rotation:45; color:theme.accent; anchors.horizontalCenter:parent.horizontalCenter; y:-4 }
                        MouseArea { x:-9;width:20;height:parent.height;cursorShape:Qt.SizeHorCursor
                            onPressed:function(mouse){var p=mapToItem(timelineColumn,mouse.x,mouse.y);root.beginTimelineScrub(p.x)}
                            onPositionChanged:function(mouse){if(pressed){var p=mapToItem(timelineColumn,mouse.x,mouse.y);root.scrubTimelineAt(p.x)}}
                            onReleased:function(mouse){var p=mapToItem(timelineColumn,mouse.x,mouse.y);root.finishTimelineScrub(p.x)}
                            onCanceled:{root.timelineScrubbing=false;root.resumeAfterTimelineScrub=false}
                        }
                    }
                    Rectangle { anchors.centerIn: parent; width: Math.min(560,parent.width-80); height: projectController.sourceUrl.toString()!==""?112:82; radius: 6; color: "#e6171c26"; border.color: theme.borderStrong; visible: timelineController.durationMs===0
                    Column { anchors.centerIn: parent; spacing:8
                        Label { anchors.horizontalCenter:parent.horizontalCenter; text:projectController.sourceUrl.toString()!==""?"SOURCE READY":"IMPORT YOUR FIRST SOURCE"; color:theme.text; font.pixelSize:18; font.weight:Font.Black; font.letterSpacing:1.2 }
                        Label { anchors.horizontalCenter:parent.horizontalCenter; text:projectController.sourceUrl.toString()!==""?"Place the entire source at 0:00, or create smaller reusable clips in Source":"Your first source automatically starts V1/A1 at 0:00"; color:theme.muted }
                        AccentButton { anchors.horizontalCenter:parent.horizontalCenter;visible:projectController.sourceUrl.toString()!=="";text:"Add Full Source to Timeline";helpText:"Place the current source's linked video and audio on V1/A1 at 0:00";onClicked:projectController.addCurrentSourceToTimeline() }
                    }
                    }
                }
            }
        }
    }
}
