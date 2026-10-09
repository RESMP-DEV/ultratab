import QtQuick
import QtQuick.Shapes

// An agent's CLI as a small glossy tile: its logo, in its own colour, on
// pitch black with a faint glow of that colour and a sheen across the top.
// `ring` (a status colour, or transparent) circles it. Every logo is drawn to
// the same optical size: fitted to its drawn bounds, scaled to one area, with
// a small correction for how heavy its ink reads. Claude Code is its mascot,
// as Claude Code draws it on its welcome screen.
Rectangle {
    id: tile
    property string harness: ""
    property color ring: "transparent"
    property real size: 24
    width: size
    height: size
    radius: size * 0.24
    border.width: 1
    border.color: "#18ffffff"
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1c1f27" }
        GradientStop { position: 0.55; color: "#07080b" }
        GradientStop { position: 1.0; color: "#000000" }
    }

    readonly property var logos: {"codex": {"box": [1.676, 1.75, 16.65, 16.5], "path": "M11.248 18.25q-.825 0-1.568-.314a4.3 4.3 0 0 1-1.32-.874 4 4 0 0 1-1.304.214 4 4 0 0 1-2.046-.544 4.27 4.27 0 0 1-1.518-1.485 4 4 0 0 1-.56-2.095q0-.48.131-1.04A4.4 4.4 0 0 1 2.04 10.71a4.07 4.07 0 0 1 .017-3.4 4.2 4.2 0 0 1 1.056-1.418 3.8 3.8 0 0 1 1.6-.842 3.9 3.9 0 0 1 .76-1.683q.593-.759 1.451-1.188a4.04 4.04 0 0 1 1.832-.429q.825 0 1.567.313.742.314 1.32.875a4 4 0 0 1 1.304-.215q1.106 0 2.046.545a4.14 4.14 0 0 1 1.501 1.485q.578.941.578 2.095 0 .48-.132 1.04.66.61 1.023 1.419.363.792.363 1.666 0 .892-.38 1.717a4.3 4.3 0 0 1-1.072 1.435 3.8 3.8 0 0 1-1.584.825 3.8 3.8 0 0 1-.775 1.683 4.06 4.06 0 0 1-1.436 1.188 4.04 4.04 0 0 1-1.832.429m-4.076-2.062q.825 0 1.435-.347l3.103-1.782a.36.36 0 0 0 .164-.313v-1.42L7.881 14.62a.67.67 0 0 1-.726 0l-3.118-1.798a.5.5 0 0 1-.017.115v.198q0 .841.396 1.551.413.693 1.139 1.089a3.2 3.2 0 0 0 1.617.412m.165-2.69a.4.4 0 0 0 .181.05q.083 0 .165-.05l1.238-.71-3.977-2.31a.7.7 0 0 1-.363-.643v-3.58q-.825.362-1.32 1.122a2.9 2.9 0 0 0-.495 1.65q0 .809.413 1.55.412.743 1.072 1.123zm3.91 3.663q.875 0 1.585-.396a2.96 2.96 0 0 0 1.534-2.64v-3.564a.32.32 0 0 0-.165-.297l-1.254-.726v4.604a.7.7 0 0 1-.363.643l-3.119 1.799a3 3 0 0 0 1.783.577m.627-6.039V8.878L10.01 7.822 8.129 8.878v2.244l1.881 1.056zM7.057 5.859a.7.7 0 0 1 .363-.644l3.119-1.798a3 3 0 0 0-1.782-.578q-.874 0-1.584.396A2.96 2.96 0 0 0 6.05 4.324a3.07 3.07 0 0 0-.396 1.551v3.547q0 .199.165.314l1.237.726zm8.383 7.887q.825-.364 1.303-1.123.495-.758.495-1.65a3.15 3.15 0 0 0-.412-1.55q-.413-.743-1.073-1.123l-3.086-1.782q-.099-.065-.181-.049a.3.3 0 0 0-.165.05l-1.238.692 3.993 2.327a.6.6 0 0 1 .264.264.64.64 0 0 1 .1.363zm-3.317-8.382a.63.63 0 0 1 .726 0l3.135 1.831v-.297q0-.792-.396-1.501a2.86 2.86 0 0 0-1.105-1.155q-.71-.43-1.65-.43-.825 0-1.436.347L8.294 5.941a.36.36 0 0 0-.165.314v1.418z", "color": "#ffffff", "optical": 1.04}, "omp": {"box": [14, 16, 36, 40], "path": "M14 16h36v8H40v32h-8V24h-6v22h-8V24h-4z", "color": "#c084fc", "optical": 0.84}, "grok": {"box": [56, 56, 400, 400], "path": "M210.484 312.759L343.465 210.383C349.984 205.364 359.302 207.322 362.408 215.117C378.758 256.231 371.454 305.64 338.925 339.563C306.397 373.487 261.137 380.927 219.768 363.983L174.577 385.803C239.394 432.008 318.104 420.581 367.289 369.251C406.303 328.564 418.386 273.104 407.088 223.091L407.19 223.198C390.807 149.726 411.218 120.359 453.03 60.3072C454.02 58.8833 455.01 57.4595 456 56L400.978 113.382V113.204L210.45 312.794 M183.042 337.641C136.519 291.294 144.54 219.567 184.236 178.203C213.59 147.59 261.683 135.096 303.666 153.464L348.755 131.75C340.632 125.627 330.221 119.042 318.275 114.414C264.277 91.2407 199.63 102.774 155.735 148.516C113.513 192.549 100.236 260.254 123.036 318.027C140.069 361.206 112.148 391.748 84.0229 422.575C74.0561 433.503 64.0553 444.431 56 456L183.007 337.677", "color": "#e5e7eb", "optical": 1.12}, "kimi": {"box": [0, 0.94, 23.95, 23.25], "path": "M21.7202 0.939941C22.9502 0.939941 23.9502 1.93994 23.9502 3.16994C23.9502 4.39994 22.9502 5.39994 21.7202 5.39994H19.7502C19.6002 5.39994 19.4902 5.27994 19.4902 5.13994V3.16994C19.4902 1.93994 20.4902 0.939941 21.7202 0.939941Z M9.39 13.9501L17.82 5.59012C17.98 5.43012 17.89 5.12012 17.68 5.12012H13.14C13.14 5.12012 13.04 5.14012 13 5.18012L3.92 14.1901C3.78 14.3301 3.57 14.2101 3.57 13.9801V5.39012C3.57 5.24012 3.47 5.12012 3.35 5.12012H0.219999C0.0999993 5.12012 0 5.24012 0 5.39012V23.9201C0 24.0701 0.0999993 24.1901 0.219999 24.1901H3.35C3.47 24.1901 3.57 24.0701 3.57 23.9201V20.1401C3.57 20.0601 3.6 19.9801 3.65 19.9301L6.47 17.1401C6.54 17.0701 6.63 17.0601 6.71 17.1101L14.24 22.6501C15.47 23.4801 16.85 23.9901 18.25 24.1401C18.37 24.1501 18.48 24.0301 18.48 23.8701V20.3101C18.48 20.1701 18.4 20.0601 18.29 20.0501C17.47 19.9201 16.66 19.6001 15.94 19.1101L9.42 14.3901C9.28 14.3001 9.27 14.0701 9.39 13.9501Z", "color": "#3b82f6", "optical": 0.96}, "opencode": {"box": [0, 0, 240, 300], "path": "M180 240H60V120H180V240Z M180 60H60V240H180V60ZM240 300H0V0H240V300Z", "color": "#f5f5f4", "optical": 0.8}, "agy": {"box": [6.579, 9.848, 86.843, 80.0], "path": "M85.2843 88.0301C90.1329 91.6664 97.4057 89.2422 90.7389 82.5755C70.7389 63.1816 74.9813 9.84827 50.1329 9.84827C25.2843 9.84827 29.5267 63.1816 9.52673 82.5755C2.25402 89.8483 10.1328 91.6664 14.9813 88.0301C33.7692 75.3028 32.5571 52.8786 50.1329 52.8786C67.7086 52.8786 66.4965 75.3028 85.2843 88.0301Z", "color": "#4f8bff", "optical": 1.0}}
    readonly property var mascot: [[3, 0], [4, 0], [5, 0], [6, 0], [7, 0], [8, 0], [9, 0], [10, 0], [11, 0], [12, 0], [13, 0], [14, 0], [15, 0], [3, 1], [4, 1], [6, 1], [7, 1], [8, 1], [9, 1], [10, 1], [11, 1], [12, 1], [14, 1], [15, 1], [1, 2], [2, 2], [3, 2], [4, 2], [5, 2], [6, 2], [7, 2], [8, 2], [9, 2], [10, 2], [11, 2], [12, 2], [13, 2], [14, 2], [15, 2], [16, 2], [17, 2], [3, 3], [4, 3], [5, 3], [6, 3], [7, 3], [8, 3], [9, 3], [10, 3], [11, 3], [12, 3], [13, 3], [14, 3], [15, 3], [3, 4], [5, 4], [13, 4], [15, 4]]
    readonly property bool isClaude: harness === "claude"
    readonly property var logo: logos[harness]
    readonly property color ink: isClaude ? "#d78787" : logo ? logo.color : "#c9d1dc"
    // Drawn size: the logo's bounds scaled to one area, kept inside the tile.
    readonly property var box: isClaude ? [1, 0, 17, 10] : logo ? logo.box : [0, 0, 1, 1]
    readonly property real optical: isClaude ? 1.0 : logo ? logo.optical : 1.0
    readonly property real scaleFactor: Math.min(size * 0.6 * optical / Math.sqrt(box[2] * box[3]),
                                                 size * 0.74 / box[2], size * 0.74 / box[3])

    // The glow of the logo's colour.
    Rectangle {
        anchors.centerIn: parent
        width: tile.size * 0.7
        height: width
        radius: width / 2
        color: Qt.rgba(tile.ink.r, tile.ink.g, tile.ink.b, 0.16)
    }
    Shape {
        visible: !tile.isClaude && tile.logo !== undefined
        x: (tile.size - tile.box[2] * tile.scaleFactor) / 2 - tile.box[0] * tile.scaleFactor
        y: (tile.size - tile.box[3] * tile.scaleFactor) / 2 - tile.box[1] * tile.scaleFactor
        scale: tile.scaleFactor
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: -1
            fillColor: tile.ink
            PathSvg { path: tile.logo ? tile.logo.path : "" }
        }
    }
    Item {
        visible: tile.isClaude
        x: (tile.size - tile.box[2] * tile.scaleFactor) / 2
        y: (tile.size - tile.box[3] * tile.scaleFactor) / 2
        Repeater {
            model: tile.isClaude ? tile.mascot : []
            delegate: Rectangle {
                required property var modelData
                x: (modelData[0] - 1) * tile.scaleFactor
                y: modelData[1] * 2 * tile.scaleFactor
                width: tile.scaleFactor + 0.4
                height: 2 * tile.scaleFactor + 0.4
                color: tile.ink
                antialiasing: false
            }
        }
    }
    Text {
        visible: !tile.isClaude && tile.logo === undefined
        anchors.centerIn: parent
        text: tile.harness.slice(0, 1).toUpperCase()
        color: tile.ink
        font.pixelSize: tile.size * 0.48
        font.weight: Font.Bold
    }
    // The sheen across the top.
    Rectangle {
        x: tile.radius * 0.5
        y: 1
        width: tile.size - tile.radius
        height: tile.size * 0.42
        radius: tile.radius * 0.8
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#26ffffff" }
            GradientStop { position: 1.0; color: "#00ffffff" }
        }
    }
    // The status ring.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -2.5
        radius: tile.radius + 2.5
        color: "transparent"
        border.width: 1.6
        border.color: tile.ring
        visible: tile.ring.a > 0
    }
}
