import QtQuick

Item {
    id: icon
    property string kind: "spark"
    property color color: "#aeb5c2"
    implicitWidth: 18
    implicitHeight: 18

    Canvas {
        id: iconCanvas
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = icon.color
            ctx.fillStyle = icon.color
            ctx.lineWidth = 1.5
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            function line(x1, y1, x2, y2) {
                ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke()
            }
            function box(x, y, w, h) {
                ctx.strokeRect(x, y, w, h)
            }

            if (icon.kind === "reverse") {
                ctx.beginPath(); ctx.arc(9, 9, 5.5, -2.5, 1.9); ctx.stroke()
                line(3.3, 4.8, 3.1, 9); line(3.3, 4.8, 7.3, 5.5)
            } else if (icon.kind === "stutter") {
                box(2, 4, 5, 10); box(7, 4, 5, 10); box(12, 4, 4, 10)
            } else if (icon.kind === "repeat") {
                line(4, 5, 13, 5); line(13, 5, 11, 3); line(13, 5, 11, 7)
                line(14, 13, 5, 13); line(5, 13, 7, 11); line(5, 13, 7, 15)
            } else if (icon.kind === "shake") {
                ctx.beginPath(); ctx.moveTo(1.5, 10); ctx.lineTo(4.5, 6); ctx.lineTo(7, 12); ctx.lineTo(10, 5); ctx.lineTo(13, 11); ctx.lineTo(16.5, 7); ctx.stroke()
            } else if (icon.kind === "freeze") {
                line(9, 2, 9, 16); line(3, 5, 15, 13); line(3, 13, 15, 5)
                line(7, 4, 9, 2); line(11, 4, 9, 2)
            } else if (icon.kind === "rhythm" || icon.kind === "audio") {
                line(1.5, 9, 4, 9); line(4, 9, 6, 4); line(6, 4, 9, 14); line(9, 14, 12, 6); line(12, 6, 14, 9); line(14, 9, 16.5, 9)
            } else if (icon.kind === "speed") {
                ctx.beginPath(); ctx.arc(9, 10, 6, 3.4, 6.0); ctx.stroke()
                line(9, 10, 13, 6); ctx.beginPath(); ctx.arc(9, 10, 1, 0, 6.3); ctx.fill()
            } else if (icon.kind === "random") {
                line(2, 5, 5, 5); line(5, 5, 13, 13); line(13, 13, 16, 13)
                line(2, 13, 5, 13); line(5, 13, 13, 5); line(13, 5, 16, 5)
                line(14, 3, 16, 5); line(14, 7, 16, 5); line(14, 11, 16, 13); line(14, 15, 16, 13)
            } else if (icon.kind === "macro") {
                box(3, 3, 9, 9); box(6, 6, 9, 9)
            } else if (icon.kind === "mix") {
                box(2, 3, 5, 5); box(11, 3, 5, 5); box(6.5, 11, 5, 5)
                line(4.5, 8, 9, 11); line(13.5, 8, 9, 11)
            } else if (icon.kind === "distort") {
                ctx.beginPath(); ctx.moveTo(1.5, 6); ctx.bezierCurveTo(5, 1, 7, 15, 10, 7); ctx.bezierCurveTo(12, 2, 14, 14, 16.5, 9); ctx.stroke()
            } else {
                line(9, 2, 9, 6); line(9, 12, 9, 16); line(2, 9, 6, 9); line(12, 9, 16, 9)
                line(4.5, 4.5, 6.5, 6.5); line(11.5, 11.5, 13.5, 13.5)
            }
        }
        Connections {
            target: icon
            function onKindChanged() { iconCanvas.requestPaint() }
            function onColorChanged() { iconCanvas.requestPaint() }
        }
    }
}
