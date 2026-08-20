pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: toolbox
    required property var controller
    required property var appTheme
    property bool hasSelection: controller && controller.selectedIds.length > 0

    function matches(value) {
        const query = searchField.text.trim().toLowerCase()
        return query === "" || String(value).toLowerCase().indexOf(query) !== -1
    }

    function visualSummary(id) {
        const values = {
            "strip_screamer":"Reordered strips + chopped stereo voice",
            "mosaic_choir":"Nine moments + widened voice crowd",
            "gravity_bass":"Central vortex + subharmonic pressure",
            "melting_voice":"Liquefied image + spectral breath",
            "newsroom_possession":"Printed broadcast + robot announcer",
            "stop_motion_robot":"Skipped movement + machine speech",
            "cell_growth":"Growing image cells + living chorus",
            "video_rot":"Eroded picture + dark crushed sound",
            "interlace_panic":"Field exchange + gated digital panic",
            "chroma_phantom":"Stolen color + wide phase ghost",
            "sonic_shockwave":"Radial impact + sub-bass detonation",
            "heatstroke":"Temporal heat + distorted bass",
            "stained_memory":"Frame stains + long delayed hall",
            "surveillance_entity":"Forensic scope + hidden radio machine",
            "cartoon_explosion":"Printed X-ray + glass transient attack",
            "reality_blender":"Funhouse vortex + stereo phase churn",
            "buffer_underrun":"Reordered frames + low-bit audio collapse",
             "cosmic_dialup":"Planetary tunnel + modem robot radio",
             "feedback_void":"Recursive feedback tunnel with glowing edges",
             "pixel_sort_crush":"Pixel sorting ruptured by digital blocks",
             "liquid_memory":"Water refraction carrying delayed light",
             "projector_break":"Gate weave, dust, flicker + frame slips",
             "analog_freefall":"Horizontal sync collapse + film drift",
             "graffiti_ghost":"Moving highlights permanently paint the frame",
             "nervous_breakdown":"Wrong-frame recall + elastic contraction",
             "cmyk_attack":"Misregistered print dots + temporal impact",
             "clone_army":"Animated full-frame clone grid",
             "dither_game":"Ordered palette texture + stepped motion",
             "elastic_reaction":"Center stretch snapping through a shockwave",
             "native_kaleido":"True radial reflection + feedback",
            "strip_tornado":"Cross-shuffled strips + orbital camera",
            "nine_lives":"Nine simultaneous neighboring moments",
            "liquid_lens":"Two-axis glass + radial ripples",
            "gravity_well":"Dark inward orbital distortion",
            "video_melt":"Liquefied columns + color residue",
            "newspaper_riot":"Shaking high-contrast print",
            "cellular_bloom":"Growing false-color image cells",
            "rotting_film":"Eroded shadows + temporal stains",
            "interlace_demon":"Exchanged luma/chroma fields",
            "stop_motion_panic":"Skipped cadence + XOR motion",
            "chroma_theft":"Stolen and rebuilt color planes",
            "shockwave":"Concentric radial impact waves",
            "heat_memory":"Solarized temporal heat burn",
            "oil_stain":"Spreading normalized color stains",
            "morph_monster":"Alternating growth + erosion",
            "strip_mine":"Block and strip spatial dismantling",
            "tunnel_vision":"Spherical central vortex",
            "crt_surgery":"Field damage under live scopes",
            "comic_freeze":"Low-cadence printed impacts",
            "dream_print":"Halftone fragments in soft waves",
            "block_party":"Shuffled rectangular image chunks",
            "fisheye_panic":"Extreme circular bubble optics",
            "tiny_planet_spin":"Rotating stereographic world",
            "scope_creep":"Live traces + engineering monitor",
            "time_scramble":"Random frame order + motion burn",
            "motion_detector":"Amplified inter-frame movement",
            "xor_nightmare":"Digital temporal silhouettes",
            "pixel_bloom_pack":"Bright spreading pixel mosaic",
            "xray_fever":"False-color directional scan",
            "slanted_universe":"Sheared space + rocking gravity",
            "impact_crater":"Crash zoom, recoil + RGB impact",
            "spin_cycle":"Rotation with burning color trails",
            "rubber_room":"Elastic warping + rocking optics",
            "signal_possession":"Tearing bands + hostile sync",
            "thermal_runaway":"Heat vision with motion burn",
            "perspective_drop":"Collapsing frame + hard push-in",
            "orbiting_ghosts":"Spinning neon afterimages",
            "glitch_shredder":"Elastic RGB slice destruction",
            "panic_cam":"Close, rocking handheld chaos",
            "afterimage_burn":"Solarized luminous motion scars",
            "deep_fried":"Extreme contrast + saturation",
            "vhs_breakdown":"Tape noise, drift + scanlines",
            "pixel_scream":"Pixel-block destruction",
            "threshold_vision":"High-contrast monochrome",
            "mirror_hell":"Mirrored rotation + lens warp",
            "acid_trip":"Cycling psychedelic color",
            "crt_meltdown":"Tube roll + RGB separation",
            "neon_pulse":"Animated neon edges + flashes",
            "memory_leak":"Long recursive frame trails",
            "vertical_sync":"Rolling sync + nervous shake",
            "cartoon_panic":"Ink edges + frantic motion",
            "solar_flare":"Solarized color bursts",
            "rgb_quake":"Channel split + heavy shake",
            "data_fever":"Datamosh smear + crossed color",
            "prism_tunnel":"Kaleidoscopic lens motion",
            "ghost_echo":"Recursive edge ghosts",
            "channel_surfer":"Noisy channel-change color",
            "hard_cutout":"Embossed threshold shapes"
        }
        return values[id] || "Stacked YTP treatment"
    }

    function audioSummary(id) {
        const values = {
            "alien_swarm":"Ring-modulated crowd of voices",
            "phase_vortex":"Wide spiraling phase motion",
            "glass_machine":"Crystalline metallic transients",
            "stereo_panic":"Extreme widening + moving echoes",
            "cyber_demon":"Low robotic saturation",
            "stadium_clone":"Huge crowd chorus + reflections",
            "robot_radio":"Band-limited robotic voice",
            "demon_bass":"Low pitch + heavy saturation",
            "chipmunk_panic":"High pitch + frantic chops",
            "haunted_hall":"Dark reverb + echo",
            "laser_voice":"Fast electronic jet flanging",
            "broken_speaker":"Hard clipping + bit crush"
        }
        return values[id] || "Creative audio stack"
    }

     function combinedSummary(id) {
         const values = {
             "feedback_scream":"Tightening feedback + rising metallic alarm",
             "sorted_voice":"Pixel-order collapse + brittle digital carrier",
             "projector_ghost":"Damaged projection + distant spectral double",
             "analog_demon":"Broken sync + low robotic transmission",
             "lightwriter":"Burned motion highlights + stereo echoes",
             "clone_chorus":"Wall of picture clones + detuned voice crowd",
             "reality_collapse":"Projection implosion + falling frequency vortex",
            "brain_scrambler":"Reordered blocks and frames + metallic speech",
            "possessed_broadcast":"Hostile signal takeover + robotic transmission",
            "vaporized":"Spectral afterimages + disembodied whisper",
            "bass_quake":"Camera impact driven by subharmonic weight",
            "time_machine":"Scrambled time + drifting phase and pitch",
            "alien_abduction":"Portal optics + rising alien voice swarm",
            "security_breach":"Forensic telemetry + clipped surveillance radio",
            "comic_impact":"X-ray zoom + crystalline transient punch",
            "dream_melt":"Liquid image trails + huge choral space",
            "digital_shred":"Torn RGB blocks + crushed robot audio",
            "void_portal":"Spinning planet projection + widening phase void"
        }
        return values[id] || "Synchronized picture and sound mutation"
    }

    function visualCategoryMatches() {
        if (matches("fx packs visual effects live trails look"))
            return true
        const presets = controller.ytpVisualPresets || []
        for (let index=0; index<presets.length; ++index) {
            const preset = presets[index]
            if (matches(preset.id + " " + preset.name + " " + preset.description + " " + visualSummary(preset.id)))
                return true
        }
        return false
    }

    function audioCategoryMatches() {
        if (matches("audio safe earrape distortion limiter loud"))
            return true
        const presets = controller.ytpAudioPresets || []
        for (let index=0; index<presets.length; ++index) {
            const preset = presets[index]
            if (matches(preset.id + " " + preset.name + " " + preset.description + " " + audioSummary(preset.id)))
                return true
        }
        return false
    }

    function combinedCategoryMatches() {
        if (matches("audio visual combined av synchronized mutation picture sound"))
            return true
        const presets = controller.ytpCombinedPresets || []
        for (let index=0; index<presets.length; ++index) {
            const preset = presets[index]
            if (matches(preset.id + " " + preset.name + " " + preset.description + " " + combinedSummary(preset.id)))
                return true
        }
        return false
    }

    function showConfig(tool, title) {
        configPopup.tool = tool
        configPopup.titleText = title
        configPopup.open()
    }

    function freezeSourceTime() {
        const inspector = controller.inspector || ({})
        return Number(inspector.sourceStartMs || 0) + Math.max(0,
            Number(controller.playheadMs || 0) - Number(inspector.startMs || 0))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TextField {
            id: searchField
            objectName: "ytpSearchField"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 7
            Layout.bottomMargin: 6
            Layout.preferredHeight: 30
            placeholderText: "Search YTP tools..."
            selectByMouse: true
            font.pixelSize: 11
            leftPadding: 9
            rightPadding: 28
            background: Rectangle {
                radius: 3
                color: toolbox.appTheme.surfaceSunken
                border.width: searchField.activeFocus ? 1 : 0
                border.color: toolbox.appTheme.cyan
            }
            ToolButton {
                visible: searchField.text !== ""
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 27; height: 27
                background: Rectangle { color:"transparent" }
                contentItem: Canvas { onPaint:{const c=getContext("2d");c.reset();c.strokeStyle=toolbox.appTheme.muted;c.lineWidth=1.4;c.beginPath();c.moveTo(9,9);c.lineTo(18,18);c.moveTo(18,9);c.lineTo(9,18);c.stroke()} }
                onClicked: searchField.clear()
                ToolTip.visible: hovered
                ToolTip.text: "Clear search"
            }
        }

        Flickable {
            id: toolScroll
            objectName: "ytpToolsPages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: width
            contentHeight: toolsColumn.implicitHeight + 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; interactive: true }
            WheelHandler {
                target: null
                blocking: true
                onWheel: function(event) {
                    const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * 44
                    toolScroll.contentY = Math.max(0, Math.min(toolScroll.contentHeight-toolScroll.height, toolScroll.contentY-delta))
                    event.accepted = true
                }
            }

            Column {
                id: toolsColumn
                width: toolScroll.width
                spacing: 2

                YtpCategoryHeader {
                    id: quickHeader
                    objectName: "ytpQuickHeader"
                    width: parent.width
                    appTheme: toolbox.appTheme
                    text: "Quick"
                    visible: toolbox.matches("quick reverse stutter repeat frame rapid freeze")
                }
                Column {
                    width: parent.width
                    visible: quickHeader.expanded && quickHeader.visible
                    spacing: 0
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Reverse";iconKind:"reverse";description:"Reverse the selected linked clip";active:Boolean(toolbox.controller.inspector.reverse);enabled:toolbox.hasSelection;visible:toolbox.matches("quick reverse direction backwards");onTriggered:toolbox.controller.setReverse(!Boolean(toolbox.controller.inspector.reverse)) }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Stutter";iconKind:"stutter";description:"Repeat a short alternating slice";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("quick stutter repeat alternating slice");onTriggered:toolbox.controller.buildStutter(6,90,true);onOptionsRequested:toolbox.showConfig("stutter","Stutter settings") }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Frame Repeat";iconKind:"repeat";description:"Hold and repeat individual frames";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("quick frame repeat hold freeze");onTriggered:toolbox.controller.buildFrameRepeat(2,5);onOptionsRequested:toolbox.showConfig("repeat","Frame Repeat settings") }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Rapid Reverse";iconKind:"reverse";description:"Alternate short forward and reverse slices";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("quick rapid reverse alternating slices");onTriggered:toolbox.controller.buildRapidReverse(8,70);onOptionsRequested:toolbox.showConfig("rapid","Rapid Reverse settings") }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:Boolean(toolbox.controller.inspector.freeze)?"Unfreeze Frame":"Freeze Frame";iconKind:"freeze";description:"Toggle a hold at the current playhead frame";active:Boolean(toolbox.controller.inspector.freeze);enabled:toolbox.hasSelection;visible:toolbox.matches("quick freeze unfreeze frame hold");onTriggered:toolbox.controller.setFreeze(!Boolean(toolbox.controller.inspector.freeze),toolbox.freezeSourceTime()) }
                }

                YtpCategoryHeader {
                    id: combinedHeader
                    objectName: "ytpCombinedHeader"
                    width: parent.width
                    appTheme: toolbox.appTheme
                    text: "Audio + Visual Mutations"
                    visible: toolbox.combinedCategoryMatches()
                }
                Column {
                    width: parent.width
                    visible: combinedHeader.expanded && combinedHeader.visible
                    spacing: 0
                    Repeater {
                        model: toolbox.controller.ytpCombinedPresets
                        YtpPackRow {
                            required property var modelData
                            width: toolsColumn.width
                            appTheme: toolbox.appTheme
                            name: modelData.name
                            description: toolbox.combinedSummary(modelData.id)
                            badge: "A/V"
                            enabled: toolbox.hasSelection
                            visible: toolbox.matches("audio visual combined mutation " + modelData.id + " " + modelData.name + " " + modelData.description + " " + description)
                            onClicked: toolbox.controller.applyYtpCombinedPreset(modelData.id)
                        }
                    }
                }

                YtpCategoryHeader {
                    id: audioHeader
                    objectName: "ytpAudioHeader"
                    width: parent.width
                    appTheme: toolbox.appTheme
                    text: "Audio"
                    visible: toolbox.audioCategoryMatches()
                }
                Column {
                    width: parent.width
                    visible: audioHeader.expanded && audioHeader.visible
                    spacing: 0
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Safe Earrape";iconKind:"distort";description:"Boost, distort, compress, and hard-limit";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("audio safe earrape distortion limiter loud");onTriggered:toolbox.controller.applySafeEarrape(.65);onOptionsRequested:toolbox.showConfig("earrape","Safe Earrape settings") }
                    Repeater {
                        model: toolbox.controller.ytpAudioPresets
                        YtpToolRow {
                            required property var modelData
                            width: toolsColumn.width
                            appTheme: toolbox.appTheme
                            text: modelData.name
                            iconKind: "audio"
                            description: toolbox.audioSummary(modelData.id)
                            enabled: toolbox.hasSelection
                            visible: toolbox.matches("audio " + modelData.id + " " + modelData.name + " " + modelData.description + " " + toolbox.audioSummary(modelData.id))
                            onTriggered: toolbox.controller.applyYtpAudioPreset(modelData.id)
                        }
                    }
                }

                YtpCategoryHeader {
                    id: remixHeader
                    objectName: "ytpRemixHeader"
                    width: parent.width
                    appTheme: toolbox.appTheme
                    text: "Timing & Remix"
                    visible: toolbox.matches("timing remix rhythm speed ladder sentence mixer randomizer macro")
                }
                Column {
                    width: parent.width
                    visible: remixHeader.expanded && remixHeader.visible
                    spacing: 0
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Rhythm Repeat";iconKind:"rhythm";description:"Repeat from the playhead on a beat grid";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("timing remix rhythm beat repeat bpm markers");onTriggered:toolbox.controller.buildRhythmRepeat(toolbox.controller.playheadMs,120,8,100,false);onOptionsRequested:toolbox.showConfig("rhythm","Rhythm Repeat settings") }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Speed Ladder";iconKind:"speed";description:"Escalate speed and pitch across repeated steps";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("timing remix speed ladder pitch steps");onTriggered:toolbox.controller.buildSpeedLadder(6,.5,3.5,2,false);onOptionsRequested:toolbox.showConfig("speed","Speed Ladder settings") }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Sentence Mixer";iconKind:"mix";description:"Reorder marker-defined phrase chunks";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("audio timing remix sentence mixer phrase words markers");onTriggered:toolbox.showConfig("sentence","Sentence Mixer");onOptionsRequested:toolbox.showConfig("sentence","Sentence Mixer") }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Seeded Randomizer";iconKind:"random";description:"Preview deterministic random clip changes";hasOptions:true;enabled:toolbox.hasSelection;visible:toolbox.matches("timing remix random randomizer shuffle seed");onTriggered:toolbox.showConfig("random","Seeded Randomizer");onOptionsRequested:toolbox.showConfig("random","Seeded Randomizer") }
                    YtpToolRow { width:parent.width;appTheme:toolbox.appTheme;text:"Macros";iconKind:"macro";description:"Record, save, and apply YTP command sequences";hasOptions:true;visible:toolbox.matches("timing remix macros automation record saved");onTriggered:toolbox.showConfig("macros","YTP Macros");onOptionsRequested:toolbox.showConfig("macros","YTP Macros") }
                }

                YtpCategoryHeader {
                    id: packsHeader
                    objectName: "ytpPacksHeader"
                    width: parent.width
                    appTheme: toolbox.appTheme
                    text: "FX Packs"
                    visible: toolbox.visualCategoryMatches()
                }
                Column {
                    width: parent.width
                    visible: packsHeader.expanded && packsHeader.visible
                    spacing: 0
                    Repeater {
                        model: toolbox.controller.ytpVisualPresets
                        YtpPackRow {
                            required property var modelData
                            width: toolsColumn.width
                            appTheme: toolbox.appTheme
                            name: modelData.name
                            description: toolbox.visualSummary(modelData.id)
                            badge: modelData.temporal ? "TRAILS" : modelData.dynamic ? "LIVE" : ""
                            enabled: toolbox.hasSelection
                            visible: toolbox.matches("fx packs effects " + modelData.id + " " + modelData.name + " " + modelData.description + " " + description + " " + badge)
                            onClicked: toolbox.controller.applyYtpVisualPreset(modelData.id)
                        }
                    }
                }
                Item { width:1;height:8 }
            }
        }
    }

    Popup {
        id: configPopup
        parent: Overlay.overlay
        property string tool: ""
        property string titleText: "YTP settings"
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(350, parent ? parent.width-24 : 350)
        height: Math.min(520, configColumn.implicitHeight + 24)
        x: parent ? Math.round((parent.width-width)/2) : 0
        y: parent ? Math.round((parent.height-height)/2) : 0
        padding: 12
        background: Rectangle { radius:6;color:toolbox.appTheme.panelRaised;border.color:toolbox.appTheme.borderStrong }

        contentItem: ColumnLayout {
            id: configColumn
            spacing: 9
            RowLayout {
                Layout.fillWidth: true
                Label { text:configPopup.titleText;Layout.fillWidth:true;color:toolbox.appTheme.text;font.pixelSize:12;font.weight:Font.DemiBold }
                ToolButton { text:"Close";font.pixelSize:10;onClicked:configPopup.close() }
            }
            Rectangle { Layout.fillWidth:true;Layout.preferredHeight:1;color:toolbox.appTheme.border }

            GridLayout {
                visible: configPopup.tool === "stutter"
                columns: 2; Layout.fillWidth:true
                Label{text:"Repeats"} SpinBox{id:stutterRepeats;from:2;to:128;value:6;Layout.fillWidth:true}
                Label{text:"Slice (ms)"} SpinBox{id:stutterSlice;from:1;to:5000;value:90;editable:true;Layout.fillWidth:true}
                CheckBox{id:stutterAlternate;text:"Alternate reverse";checked:true;Layout.columnSpan:2}
                Button{text:"Build Stutter";Layout.columnSpan:2;Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:{toolbox.controller.buildStutter(stutterRepeats.value,stutterSlice.value,stutterAlternate.checked);configPopup.close()}}
            }
            GridLayout {
                visible: configPopup.tool === "repeat"
                columns:2;Layout.fillWidth:true
                Label{text:"Source frames"} SpinBox{id:repeatFrames;from:1;to:120;value:2;Layout.fillWidth:true}
                Label{text:"Holds / frame"} SpinBox{id:repeatHolds;from:1;to:120;value:5;Layout.fillWidth:true}
                Button{text:"Build Frame Repeat";Layout.columnSpan:2;Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:{toolbox.controller.buildFrameRepeat(repeatFrames.value,repeatHolds.value);configPopup.close()}}
            }
            GridLayout {
                visible: configPopup.tool === "rapid"
                columns:2;Layout.fillWidth:true
                Label{text:"Segments"} SpinBox{id:rapidSegments;from:2;to:128;value:8;Layout.fillWidth:true}
                Label{text:"Slice (ms)"} SpinBox{id:rapidSlice;from:1;to:5000;value:70;editable:true;Layout.fillWidth:true}
                Button{text:"Build Rapid Reverse";Layout.columnSpan:2;Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:{toolbox.controller.buildRapidReverse(rapidSegments.value,rapidSlice.value);configPopup.close()}}
            }
            GridLayout {
                visible: configPopup.tool === "rhythm"
                columns:2;Layout.fillWidth:true
                Label{text:"Tempo BPM"} SpinBox{id:rhythmBpm;from:20;to:400;value:120;editable:true;Layout.fillWidth:true}
                Label{text:"Beats"} SpinBox{id:rhythmBeats;from:1;to:256;value:8;Layout.fillWidth:true}
                Label{text:"Gate (ms)"} SpinBox{id:rhythmGate;from:1;to:5000;value:100;editable:true;Layout.fillWidth:true}
                CheckBox{id:rhythmMarkers;text:"Use timeline markers";Layout.columnSpan:2}
                Button{text:"Repeat from Playhead";Layout.columnSpan:2;Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:{toolbox.controller.buildRhythmRepeat(toolbox.controller.playheadMs,rhythmBpm.value,rhythmBeats.value,rhythmGate.value,rhythmMarkers.checked);configPopup.close()}}
            }
            GridLayout {
                visible: configPopup.tool === "speed"
                columns:2;Layout.fillWidth:true
                Label{text:"Steps"} SpinBox{id:speedSteps;from:2;to:64;value:6;Layout.fillWidth:true}
                Label{text:"Start speed %"} SpinBox{id:speedStart;from:5;to:1600;value:50;editable:true;Layout.fillWidth:true}
                Label{text:"End speed %"} SpinBox{id:speedEnd;from:5;to:1600;value:350;editable:true;Layout.fillWidth:true}
                Label{text:"Pitch / step"} SpinBox{id:speedPitch;from:-24;to:24;value:2;editable:true;Layout.fillWidth:true}
                CheckBox{id:speedPreserve;text:"Preserve pitch";Layout.columnSpan:2}
                Button{text:"Build Speed Ladder";Layout.columnSpan:2;Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:{toolbox.controller.buildSpeedLadder(speedSteps.value,speedStart.value/100,speedEnd.value/100,speedPitch.value,speedPreserve.checked);configPopup.close()}}
            }
            ColumnLayout {
                visible: configPopup.tool === "earrape"
                Layout.fillWidth:true
                Label{text:"Intensity";color:toolbox.appTheme.muted}
                Slider{id:earrapeIntensity;from:0;to:1;value:.65;Layout.fillWidth:true}
                Button{text:"Apply Safe Earrape";Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:{toolbox.controller.applySafeEarrape(earrapeIntensity.value);configPopup.close()}}
            }
            ColumnLayout {
                visible: configPopup.tool === "sentence"
                Layout.fillWidth:true
                Label{text:"Place markers inside the phrase, then enter chunk order.";wrapMode:Text.Wrap;Layout.fillWidth:true;color:toolbox.appTheme.muted;font.pixelSize:10}
                TextField{id:sentenceOrder;placeholderText:"Order, e.g. 2,0,1,1";Layout.fillWidth:true}
                Button{text:"Mix Marked Chunks";Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:{toolbox.controller.buildSentenceMixer(sentenceOrder.text);configPopup.close()}}
            }
            GridLayout {
                visible: configPopup.tool === "random"
                columns:2;Layout.fillWidth:true
                Label{text:"Seed"} SpinBox{id:randomSeed;from:1;to:2147483647;value:1337;editable:true;Layout.fillWidth:true}
                Label{text:"Reverse %"} SpinBox{id:randomReverse;from:0;to:100;value:35;Layout.fillWidth:true}
                Label{text:"Effect %"} SpinBox{id:randomEffects;from:0;to:100;value:30;Layout.fillWidth:true}
                Label{text:"Min / max speed %"}
                RowLayout{SpinBox{id:randomMinSpeed;from:5;to:1600;value:50;Layout.fillWidth:true}SpinBox{id:randomMaxSpeed;from:5;to:1600;value:250;Layout.fillWidth:true}}
                Label{text:"Min / max pitch"}
                RowLayout{SpinBox{id:randomMinPitch;from:-48;to:48;value:-12;Layout.fillWidth:true}SpinBox{id:randomMaxPitch;from:-48;to:48;value:12;Layout.fillWidth:true}}
                CheckBox{id:randomShuffle;text:"Shuffle positions";checked:true;Layout.columnSpan:2}
                RowLayout{Layout.columnSpan:2;Layout.fillWidth:true
                    Button{text:"Preview";Layout.fillWidth:true;enabled:toolbox.hasSelection;onClicked:toolbox.controller.previewRandomizer(randomSeed.value,randomReverse.value/100,randomEffects.value/100,randomMinSpeed.value/100,randomMaxSpeed.value/100,randomMinPitch.value,randomMaxPitch.value,randomShuffle.checked)}
                    Button{text:"Commit";enabled:(toolbox.controller.randomizerPreview.changeCount||0)>0;onClicked:{toolbox.controller.commitRandomizer();configPopup.close()}}
                    Button{text:"Cancel";enabled:(toolbox.controller.randomizerPreview.changeCount||0)>0;onClicked:toolbox.controller.cancelRandomizer()}
                }
                Label{text:toolbox.controller.randomizerPreview.summary||"No preview";Layout.columnSpan:2;Layout.fillWidth:true;color:toolbox.appTheme.muted;font.pixelSize:9;elide:Text.ElideRight}
            }
            ColumnLayout {
                visible: configPopup.tool === "macros"
                Layout.fillWidth:true
                RowLayout{Layout.fillWidth:true
                    Button{text:toolbox.controller.macroRecording?"Stop":"Record";onClicked:toolbox.controller.macroRecording?toolbox.controller.cancelMacroRecording():toolbox.controller.startMacroRecording()}
                    TextField{id:macroName;placeholderText:"Macro name";Layout.fillWidth:true}
                    Button{text:"Save";enabled:toolbox.controller.macroRecording&&toolbox.controller.recordedMacroSteps>0;onClicked:toolbox.controller.saveRecordedMacro(macroName.text)}
                }
                Label{text:toolbox.controller.recordedMacroSteps+" recorded steps";color:toolbox.appTheme.muted;font.pixelSize:9;visible:toolbox.controller.macroRecording}
                Repeater{model:toolbox.controller.ytpMacros
                    RowLayout{id:macroRow;required property string modelData;Layout.fillWidth:true;Label{text:macroRow.modelData;Layout.fillWidth:true;elide:Text.ElideRight}Button{text:"Apply";enabled:toolbox.hasSelection;onClicked:toolbox.controller.applyYtpMacro(macroRow.modelData)}Button{text:"Remove";onClicked:toolbox.controller.removeYtpMacro(macroRow.modelData)}}
                }
            }
        }
    }
}
