import QtQuick

Item {
    id: strip
    property url source
    property var thumbnailProvider
    property int cacheGeneration: 0
    property string mediaId
    property real sourceStartMs: 0
    property real sourceEndMs: 0
    property real playbackRate: 1
    property bool reverse: false
    property bool freeze: false
    property real timelineStartMs: 0
    property real viewportStartMs: 0
    property real viewportEndMs: 0
    property real pixelsPerSecond: 80
    property int sourceFrameCount: 32
    property int sourceFrameWidth: 128
    property int sourceFrameHeight: 72
    property real cellWidth: 72
    property real cellHeight: 40

    readonly property int totalFrameCount: Math.max(1, Math.ceil(width / cellWidth))
    readonly property real visibleStartX: Math.max(0,
        (viewportStartMs - timelineStartMs) * pixelsPerSecond / 1000)
    readonly property real visibleEndX: Math.min(width,
        (viewportEndMs - timelineStartMs) * pixelsPerSecond / 1000)
    readonly property int firstVisibleFrame: Math.max(0, Math.min(totalFrameCount - 1,
        Math.floor(visibleStartX / cellWidth)))
    readonly property int lastVisibleFrame: Math.max(firstVisibleFrame, Math.min(totalFrameCount - 1,
        Math.ceil(Math.max(visibleStartX, visibleEndX) / cellWidth)))
    readonly property int visibleFrameCount: lastVisibleFrame - firstVisibleFrame + 1
    clip: true

    function sourceTimeForCell(frameIndex) {
        if (freeze)
            return sourceStartMs
        const timelineOffsetMs = frameIndex * cellWidth * 1000 / Math.max(1, pixelsPerSecond)
        const sourceOffsetMs = timelineOffsetMs * Math.max(0.000001, playbackRate)
        if (reverse)
            return Math.max(sourceStartMs, sourceEndMs - sourceOffsetMs)
        return Math.min(Math.max(sourceStartMs, sourceEndMs - 1), sourceStartMs + sourceOffsetMs)
    }

    Row {
        x: strip.firstVisibleFrame * strip.cellWidth
        width: strip.visibleFrameCount * strip.cellWidth
        height: parent.height
        spacing: 0
        Repeater {
            model: strip.visibleFrameCount
            Item {
                id: cell
                objectName: "timelineThumbnailCell"
                required property int index
                readonly property int frameIndex: strip.firstVisibleFrame + index
                readonly property real sourceTimeMs: strip.sourceTimeForCell(frameIndex)
                readonly property int fallbackIndex: Math.max(0, Math.min(strip.sourceFrameCount - 1,
                    Math.round((sourceTimeMs - strip.sourceStartMs) /
                               Math.max(1, strip.sourceEndMs - strip.sourceStartMs) *
                               (strip.sourceFrameCount - 1))))
                readonly property url exactSource: {
                    const generation = strip.cacheGeneration
                    return strip.thumbnailProvider && strip.mediaId !== ""
                        ? strip.thumbnailProvider.sourceThumbnailUrl(strip.mediaId, Math.round(sourceTimeMs))
                        : ""
                }
                width: strip.cellWidth
                height: strip.height
                clip: true

                Image {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: strip.cellWidth
                    height: Math.min(strip.cellHeight, parent.height)
                    visible: cell.exactSource.toString() === ""
                    source: strip.source
                    sourceSize.width: strip.sourceFrameCount * strip.sourceFrameWidth
                    sourceSize.height: strip.sourceFrameHeight
                    sourceClipRect: Qt.rect(cell.fallbackIndex * strip.sourceFrameWidth, 0,
                                            strip.sourceFrameWidth, strip.sourceFrameHeight)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    smooth: false
                    mipmap: false
                }
                Image {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: strip.cellWidth
                    height: Math.min(strip.cellHeight, parent.height)
                    visible: cell.exactSource.toString() !== ""
                    source: cell.exactSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    smooth: false
                    mipmap: false
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: 1
                    height: Math.min(strip.cellHeight, parent.height)
                    color: "#40000000"
                }
            }
        }
    }
}
