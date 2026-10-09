import QtQuick

// Ultra Tab's overlay: a compact command bar for the agents that need you.
// The top line is the reply: lapis's guess shows as ghost text that Tab
// sends, and typing replaces it (Return sends). The card below says what
// happened, and the footer's lights show every agent by how much it needs
// you. Every card takes the same four answers: Tab accepts, holding Option
// speaks (a placeholder for now), typing then Return sends, and the left
// arrow or Delete skips. Input is never deferred for motion: the deck changes
// at once and the animation only follows it.
// Context: `deck` (Deck), `host` (OverlayHost: where the window puts the blur
// and how large it is), `backdrop` (draw a stand-in for the blurred desktop,
// for captures), `reducedMotion`.
Item {
    id: root
    objectName: "overlay"
    width: 720
    height: 560

    readonly property color dim: "#8a93a3"
    readonly property color faint: "#5f6878"
    readonly property color text: "#e9edf3"
    readonly property color body: "#b9c2cf"
    readonly property color lapisBlue: "#5b8cff"
    readonly property color lapisDeep: "#1d3f8a"
    readonly property color gold: "#e8b931"
    readonly property color green: "#3ecf8e"
    readonly property color red: "#ef6b5b"
    readonly property color violet: "#a77bff"
    readonly property string mono: "Menlo"
    // Category colours by the category's place; a request is red.
    readonly property var hues: ["#e8b931", "#5b8cff", "#a77bff", "#2ec4b6", "#f472b6", "#f59e0b"]
    function hueColor(hue) { return hue < 0 ? red : hues[hue % hues.length] }
    // How much a card needs the person, the same everywhere: red needs you,
    // yellow could use a nudge, green is running and needs nothing.
    readonly property color needsColor: "#ef5b4f"
    readonly property color steerColor: "#f2c230"
    readonly property color runningColor: "#3ecf8e"
    function weightColor(weight) { return weight >= 2 ? needsColor : weight === 1 ? steerColor : runningColor }
    // Command-Return: a push back, written in the larger box below the card.
    property bool pushing: false
    onPushingChanged: {
        if (pushing) {
            pushText.text = entry.text
            entry.text = ""
            pushText.forceActiveFocus()
            pushText.cursorPosition = pushText.length
        } else {
            entry.forceActiveFocus()
        }
    }

    readonly property int motion: reducedMotion ? 0 : 200
    readonly property var front: deck.front
    readonly property var queue: deck.queue
    readonly property bool hasFront: front.key !== undefined
    readonly property bool typing: entry.text.length > 0 || entry.inputMethodComposing
                                   || pushing
    property alias typed: entry.text
    // The card being typed to stays in front until the text is sent or cleared.
    onTypingChanged: deck.setDrafting(typing)
    // The key that was just pressed, lit on its keycap for a moment.
    property string flash: ""
    property bool optionHeld: false
    // Option speaks only after it has been up once since the overlay took the
    // keyboard: the Option of the summoning chord never lights or listens.
    property bool optionArmed: true
    Connections {
        target: root.Window.window
        ignoreUnknownSignals: true
        function onActiveChanged() {
            if (root.Window.window && root.Window.window.active) {
                root.optionArmed = !deck.optionDown()
                root.optionHeld = false
                holdToSpeak.stop()
            }
        }
    }

    // The panel; the window is as large as it and the blur covers it only.
    readonly property int panelWidth: Math.min(680, width - 40)
    // The screen bounds the card, never the window, which follows the card.
    // desktopAvailableHeight is the whole virtual desktop, so beside a taller
    // display this laid the card out to a height the window cannot show; the
    // window follows the card, so size both from the screen this one is on.
    readonly property real tallest: {
        const screen = Screen.height > 0 ? Screen.height : 1000
        // Chrome around the panel: panel.y, the peeking edges and the bottom
        // margin, about 56 points in all.
        return Math.max(220, screen - 56)
    }
    // Grows with the card at once; shrinks only after the motion, so the
    // window never cuts the card while it animates.
    property real windowPanelHeight: panel.targetHeight
    Connections {
        target: panel
        function onTargetHeightChanged() {
            if (root.settling || panel.targetHeight >= root.windowPanelHeight)
                root.windowPanelHeight = panel.targetHeight
            else
                shrinkLater.restart()
        }
    }
    Timer {
        id: shrinkLater
        interval: root.motion + 60
        onTriggered: root.windowPanelHeight = panel.targetHeight
    }
    readonly property int contentHeight: Math.ceil(panel.y + windowPanelHeight + 24)
    onContentHeightChanged: tellHost()
    Component.onCompleted: tellHost()
    function tellHost() {
        if (typeof host === "undefined" || host === null)
            return
        host.setContentSize(720, contentHeight)
        host.setPanel(Qt.rect(panel.x, panel.y, panel.width, panel.height), panel.radius)
    }

    // Opening: everything settles where it belongs with no card motion; the
    // window fades in as a whole. Answers after that animate as usual.
    property bool settling: false
    Connections {
        target: typeof host !== "undefined" ? host : null
        ignoreUnknownSignals: true
        function onAppearing() {
            root.settling = true
            arrive.stop()
            leave.stop()
            ghost.opacity = 0
            arrivalLine.opacity = 0
            glow.opacity = 0
            frontBody.opacity = 1
            arriveShift.y = 0
            root.windowPanelHeight = panel.targetHeight
            settled.restart()
        }
    }
    Timer {
        id: settled
        interval: 200
        onTriggered: root.settling = false
    }

    // Captures only: what the real window's blur would show through.
    Rectangle {
        anchors.fill: parent
        z: -10
        visible: backdrop
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#2a2147" }
            GradientStop { position: 0.55; color: "#0e1522" }
            GradientStop { position: 1.0; color: "#0c3a46" }
        }
    }

    component Keycap: Rectangle {
        id: cap
        property string glyph
        property bool lit: false
        property color accent: root.dim
        property int glyphSize: 11
        implicitWidth: Math.max(22, glyphText.implicitWidth + 10)
        implicitHeight: 20
        radius: 5
        color: lit ? Qt.rgba(accent.r, accent.g, accent.b, 0.28) : "#14ffffff"
        border.color: lit ? accent : "#1fffffff"
        Behavior on color { ColorAnimation { duration: reducedMotion ? 0 : 90 } }
        Text {
            id: glyphText
            anchors.centerIn: parent
            text: cap.glyph
            color: cap.lit ? "#ffffff" : "#cfd6e2"
            font.pixelSize: cap.glyphSize
            font.weight: Font.DemiBold
        }
    }

    // The agents in one light of the footer, each as its CLI's tile in that
    // light's colour; hovering one names it.
    component Light: Rectangle {
        id: light
        property var agents: []
        property color tint
        property string name
        objectName: name
        visible: agents.length > 0
        height: 28
        width: lights.implicitWidth + 12
        radius: 14
        color: Qt.rgba(tint.r, tint.g, tint.b, 0.08)
        border.color: Qt.rgba(tint.r, tint.g, tint.b, 0.35)
        Row {
            id: lights
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Repeater {
                model: light.agents
                delegate: HarnessTile {
                    required property var modelData
                    anchors.verticalCenter: parent.verticalCenter
                    size: modelData.front ? 20 : 18
                    harness: modelData.harness
                    ring: light.tint
                    opacity: modelData.front || light.name === "runningLight" ? 1 : 0.8
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: root.hint = modelData.name
                    }
                }
            }
        }
    }

    component Paragraph: Text {
        width: parent ? parent.width : 0
        color: root.body
        font.pixelSize: 14
        lineHeight: 1.3
        wrapMode: Text.Wrap
        maximumLineCount: 5
        elide: Text.ElideRight
        textFormat: Text.PlainText
    }

    component ListBlock: Column {
        id: list
        property var block
        spacing: 5
        Repeater {
            model: list.block.items.slice(0, 6)
            delegate: Item {
                required property string modelData
                width: parent.width
                height: item.implicitHeight
                Rectangle {
                    x: 3
                    y: 8
                    width: 5
                    height: 5
                    radius: 2.5
                    color: root.lapisBlue
                }
                Text {
                    id: item
                    x: 18
                    width: parent.width - 18
                    text: modelData
                    color: root.body
                    font.pixelSize: 14
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }
    }

    // Aligned columns: numbers right-aligned in tabular figures, a quiet
    // header, hairlines between rows and no zebra striping.
    component TableBlock: Column {
        id: table
        property var block
        readonly property int pad: 14
        // Laid out imperatively: measuring sets TextMetrics' text, which a
        // binding would read back as a loop.
        property var widths: []
        readonly property real contentWidth: widths.reduce((sum, value) => sum + value, 0)
        onWidthChanged: relayout()
        onBlockChanged: relayout()
        Component.onCompleted: relayout()
        function relayout() {
            if (block && width > 0)
                widths = layoutColumns(width)
        }
        spacing: 0
        TextMetrics { id: cellMetrics; font.family: Qt.application.font.family; font.pixelSize: 14; font.features: ({ "tnum": 1 }) }
        TextMetrics { id: headMetrics; font.family: Qt.application.font.family; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 0.5 }
        function natural(column) {
            headMetrics.text = block.columns[column]
            let widest = headMetrics.advanceWidth
            for (const row of block.rows) {
                cellMetrics.text = row[column]
                widest = Math.max(widest, cellMetrics.advanceWidth)
            }
            return Math.ceil(widest) + pad * 2
        }
        function layoutColumns(available) {
            const count = block.columns.length
            let wanted = []
            let total = 0
            for (let column = 0; column < count; ++column) {
                wanted.push(natural(column))
                total += wanted[column]
            }
            if (total > available) {
                // Numbers keep their width; text columns give way and elide.
                let flexible = 0
                for (let column = 0; column < count; ++column)
                    if (!block.numeric[column])
                        flexible += wanted[column]
                const excess = total - available
                for (let column = 0; column < count; ++column)
                    if (!block.numeric[column] && flexible > 0)
                        wanted[column] = Math.max(72, wanted[column] - excess * wanted[column] / flexible)
            }
            return wanted
        }
        Row {
            height: 30
            Repeater {
                model: table.block.columns
                delegate: Text {
                    required property string modelData
                    required property int index
                    width: table.widths[index]
                    height: 30
                    leftPadding: table.pad
                    rightPadding: table.pad
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: table.block.numeric[index] ? Text.AlignRight : Text.AlignLeft
                    text: modelData
                    color: root.dim
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.5
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }
        Rectangle { width: table.contentWidth; height: 1; color: "#33ffffff" }
        Repeater {
            model: table.block.rows
            delegate: Column {
                required property var modelData
                required property int index
                Row {
                    height: 30
                    Repeater {
                        model: modelData
                        delegate: Text {
                            required property string modelData
                            required property int index
                            width: table.widths[index]
                            height: 30
                            leftPadding: table.pad
                            rightPadding: table.pad
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: table.block.numeric[index] ? Text.AlignRight : Text.AlignLeft
                            text: modelData
                            color: index === 0 ? root.text : root.body
                            font.pixelSize: 14
                            font.features: ({ "tnum": 1 })
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                }
                Rectangle {
                    visible: index < table.block.rows.length - 1
                    width: table.contentWidth
                    height: 1
                    color: "#12ffffff"
                }
            }
        }
        Text {
            visible: table.block.more > 0
            topPadding: 6
            leftPadding: table.pad
            text: "+" + table.block.more + " more rows"
            color: root.faint
            font.pixelSize: 12
        }
    }

    component DiagramBlock: Rectangle {
        id: panel
        property var block
        radius: 10
        color: "#0affffff"
        border.color: "#16ffffff"
        implicitHeight: picture.height + 24
        Image {
            id: picture
            objectName: "diagram"
            x: 12
            y: 12
            width: parent.width - 24
            height: Math.min(200, width / Math.max(0.2, panel.block.aspect))
            fillMode: Image.PreserveAspectFit
            sourceSize.width: width * 2
            sourceSize.height: height * 2
            source: panel.block.source
            smooth: true
            cache: false
        }
    }

    component LinkChip: Rectangle {
        id: chip
        property var block
        objectName: "link"
        width: Math.min(parent ? parent.width : 400, chipRow.implicitWidth + 24)
        height: 32
        radius: 8
        color: "#101b30"
        border.color: root.lapisDeep
        Row {
            id: chipRow
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "↗"
                color: root.lapisBlue
                font.pixelSize: 14
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.block.label
                color: "#d6e3ff"
                font.pixelSize: 14
                width: Math.min(implicitWidth, chip.parent ? chip.parent.width * 0.5 : 300)
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.block.target
                color: root.faint
                font.family: root.mono
                font.pixelSize: 12
                width: Math.min(implicitWidth, chip.parent ? chip.parent.width * 0.3 : 200)
                elide: Text.ElideMiddle
                textFormat: Text.PlainText
            }
            Keycap {
                visible: chip.block.index === 0
                anchors.verticalCenter: parent.verticalCenter
                glyph: "⌘O"
                glyphSize: 10
            }
        }
        // Command-click opens it; a plain click only says how.
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                if (mouse.modifiers & Qt.ControlModifier)
                    deck.openLink(chip.block.index)
                else
                    root.hint = "⌘-click or ⌘O opens " + chip.block.label
            }
        }
    }
    property string hint: ""

    // What happened on one card: its agent, the headline and the blocks.
    // The leaving copy draws a frozen card with the same component.
    component CardBody: Column {
        id: cardBody
        property var card: ({})
        readonly property bool shown: card.key !== undefined
        spacing: 10
        Text {
            objectName: cardBody.objectName === "frontBody" ? "agentName" : ""
            width: parent.width
            text: cardBody.shown ? cardBody.card.name : ""
            color: root.dim
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        Text {
            objectName: cardBody.objectName === "frontBody" ? "line" : ""
            width: parent.width
            text: cardBody.shown ? cardBody.card.headline : ""
            color: "#ffffff"
            font.pixelSize: 18
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.15
            textFormat: Text.PlainText
        }
        Column {
            id: blocks
            objectName: cardBody.objectName === "frontBody" ? "blocks" : ""
            width: parent.width
            spacing: 10
            visible: cardBody.shown && cardBody.card.blocks.length > 0
            Repeater {
                model: cardBody.shown ? cardBody.card.blocks : []
                delegate: Loader {
                    required property var modelData
                    width: blocks.width
                    sourceComponent: modelData.type === "text" ? textBlock
                                   : modelData.type === "list" ? listBlock
                                   : modelData.type === "table" ? tableBlock
                                   : modelData.type === "diagram" ? diagramBlock
                                   : linkBlock
                    property var block: modelData
                }
            }
            Component { id: textBlock; Paragraph { objectName: "textBlock"; text: parent.block.text } }
            Component { id: listBlock; ListBlock { objectName: "listBlock"; width: parent.width; block: parent.block } }
            Component { id: tableBlock; TableBlock { objectName: "tableBlock"; width: parent.width; block: parent.block } }
            Component { id: diagramBlock; DiagramBlock { objectName: "diagramBlock"; width: parent.width; block: parent.block } }
            Component { id: linkBlock; Item { width: parent.width; implicitHeight: 30; LinkChip { block: parent.parent.block } } }
        }
    }

    Rectangle {
        id: panel
        objectName: "panel"
        x: (root.width - width) / 2
        y: 12
        width: root.panelWidth
        // The card's own height; the panel follows it with a short ease.
        readonly property real targetHeight: Math.min(column.implicitHeight, root.tallest)
        height: targetHeight
        Behavior on height {
            enabled: !reducedMotion && !root.settling
            NumberAnimation { duration: root.motion; easing.type: Easing.OutCubic }
        }
        onHeightChanged: root.tellHost()
        onWidthChanged: root.tellHost()
        onXChanged: root.tellHost()
        radius: 14
        // A dark, mostly opaque tint over the window's blur, as Raycast's
        // default: the desktop shows only as a faint softened glow.
        color: Qt.rgba(22 / 255, 23 / 255, 28 / 255, 0.90)
        border.width: root.pushing ? 1.5 : 1
        border.color: root.pushing ? Qt.rgba(239 / 255, 91 / 255, 79 / 255, 0.6) : "#17ffffff"
        clip: true

        // The answer's colour, flashing on the edge as the card leaves.
        Rectangle {
            id: glow
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1.5
            border.color: root.green
            opacity: 0
        }
        // The incoming agent's colour along the top, fading as it settles.
        Rectangle {
            id: arrivalLine
            width: parent.width
            height: 2
            color: root.gold
            opacity: 0
        }

        // The window moves when the panel's background is dragged, snapping
        // to the screen's center line and a few heights (host.dragTo).
        MouseArea {
            objectName: "dragArea"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            property point grab
            onPressed: (mouse) => {
                const window = root.Window.window
                const at = mapToGlobal(mouse.x, mouse.y)
                if (window)
                    grab = Qt.point(at.x - window.x, at.y - window.y)
                mouse.accepted = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed || typeof host === "undefined" || host === null)
                    return
                const at = mapToGlobal(mouse.x, mouse.y)
                host.dragTo(Math.round(at.x - grab.x), Math.round(at.y - grab.y), false)
            }
            onReleased: (mouse) => {
                if (typeof host === "undefined" || host === null)
                    return
                const window = root.Window.window
                if (window)
                    host.dragTo(window.x, window.y, true)
            }
        }
        // The guides while dragging: the center line, and the panel's edge
        // when it sits at one of the set heights.
        Rectangle {
            objectName: "centerGuide"
            visible: typeof host !== "undefined" && host !== null && host.dragging && host.centered
            x: parent.width / 2 - 0.5
            width: 1
            height: parent.height
            color: root.gold
            opacity: 0.8
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1.5
            border.color: root.gold
            visible: typeof host !== "undefined" && host !== null && host.dragging && host.level
        }

        Column {
            id: column
            width: parent.width

            // The reply line: the agent's mark, then the guess as ghost text
            // or what is being typed.
            Item {
                id: replyLine
                objectName: "replyLine"
                width: parent.width
                height: root.hasFront ? Math.max(54, entry.implicitHeight + 30) : 0
                // Hidden by opacity, never `visible`, like the entry itself: an
                // invisible item cannot hold focus, and with an empty deck the
                // entry has to keep the keyboard for Escape and the category
                // keys to reach the overlay at all.
                opacity: root.hasFront ? 1 : 0
                HarnessTile {
                    id: frontMark
                    objectName: "frontTile"
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    size: 26
                    harness: root.hasFront ? root.front.harness : ""
                    ring: root.hasFront ? root.weightColor(root.front.weight) : "transparent"
                }
                Text {
                    id: proposal
                    objectName: "proposal"
                    x: entry.x
                    width: entry.width
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.typing && !deck.listening
                    text: !root.hasFront ? ""
                          : root.front.request ? "A request is waiting. Answer it in the agent's own window."
                          : root.front.proposal.length > 0 ? root.front.proposal
                          : "Type a reply"
                    color: root.hasFront && root.front.canAccept ? "#9aa3b2" : root.faint
                    font.family: root.hasFront && root.front.canAccept ? root.mono : Qt.application.font.family
                    font.pixelSize: 15
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                TextInput {
                    id: entry
                    objectName: "entry"
                    x: frontMark.x + frontMark.width + 12
                    width: parent.width - x - keyHints.width - 30
                    anchors.verticalCenter: parent.verticalCenter
                    // Hidden by opacity, never `visible`, so it keeps the keyboard.
                    opacity: deck.listening ? 0 : 1
                    focus: true
                    color: "#ffffff"
                    selectionColor: "#3b4a70"
                    font.pixelSize: 15
                    wrapMode: TextInput.Wrap
                    cursorVisible: true
                    Keys.onPressed: (event) => root.handleKey(event)
                    Keys.onReleased: (event) => {
                        if (event.key === Qt.Key_Alt && !event.isAutoRepeat) {
                            root.optionArmed = true
                            holdToSpeak.stop()
                            root.optionHeld = false
                            deck.setListening(false)
                        }
                    }
                }
                Row {
                    x: entry.x
                    anchors.verticalCenter: parent.verticalCenter
                    visible: deck.listening
                    spacing: 9
                    Row {
                        spacing: 3
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: 4
                            delegate: Rectangle {
                                required property int index
                                width: 3
                                radius: 1.5
                                color: root.violet
                                anchors.verticalCenter: parent.verticalCenter
                                height: 6 + (index % 2) * 8
                            }
                        }
                    }
                    Text {
                        objectName: "listening"
                        text: "listening"
                        color: root.violet
                        font.pixelSize: 15
                    }
                }
                // What the keys do now: Tab and left at rest; Return and
                // Command-Return while a reply is being written.
                Row {
                    id: keyHints
                    objectName: "keyHints"
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Keycap { visible: !root.typing && root.hasFront && root.front.canAccept; glyph: "⇥"; accent: root.green; lit: root.flash === "tab" }
                    Keycap { visible: !root.typing; glyph: "←"; accent: root.red; lit: root.flash === "left" }
                    Keycap { visible: root.typing && !root.pushing; glyph: "↵"; accent: root.green }
                    Keycap { visible: root.typing && root.hasFront && root.front.canType; glyph: "⌘↵"; accent: root.needsColor; lit: root.pushing }
                    Text {
                        visible: root.pushing
                        anchors.verticalCenter: parent.verticalCenter
                        text: "push back"
                        color: "#ff8f84"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: "#14ffffff"; visible: root.hasFront }

            // The card itself.
            Item {
                id: cardArea
                objectName: "frontCard"
                width: parent.width
                height: root.hasFront ? frontBody.implicitHeight + 30
                                         + (root.pushing ? pushBox.height + 12 : 0) : 0
                visible: root.hasFront
                CardBody {
                    id: frontBody
                    objectName: "frontBody"
                    x: 18
                    y: 14
                    width: parent.width - 36 - lapisMap.width - 16
                    card: root.front
                    transform: Translate { id: arriveShift }
                }
                // Where this agent sits in lapis: one column per category in
                // its colour, one block per agent in strip order, this one lit.
                // Clicking it, or Command-L, shows the agent in lapis.
                Column {
                    id: lapisMap
                    objectName: "lapisMap"
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    y: 14
                    spacing: 5
                    Row {
                        spacing: 4
                        Repeater {
                            model: deck.map
                            delegate: Column {
                                required property var modelData
                                spacing: 2
                                Rectangle { width: 12; height: 3; radius: 1.5; color: root.hueColor(modelData.hue) }
                                Repeater {
                                    model: modelData.slots
                                    delegate: Rectangle {
                                        required property var modelData
                                        objectName: modelData.lit ? "lapisMapLit" : ""
                                        width: 12
                                        height: 7
                                        radius: 2
                                        color: modelData.lit ? "#e9edf3" : "#22ffffff"
                                        border.width: modelData.lit ? 1 : 0
                                        border.color: "#ffffff"
                                    }
                                }
                            }
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        spacing: 4
                        Keycap { glyph: "⌘L"; glyphSize: 10 }
                        Text { text: "lapis"; color: root.dim; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                MouseArea {
                    anchors.fill: lapisMap
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openInLapis()
                }
                // The push back: a larger box for a correction.
                Rectangle {
                    id: pushBox
                    objectName: "pushBox"
                    visible: root.pushing
                    x: 18
                    y: frontBody.y + frontBody.implicitHeight + 12
                    width: parent.width - 36
                    height: Math.max(96, pushText.contentHeight + 22)
                    radius: 9
                    color: "#33000000"
                    border.color: "#55ef5b4f"
                    TextEdit {
                        id: pushText
                        objectName: "pushText"
                        x: 12
                        y: 10
                        width: parent.width - 24
                        color: "#ffffff"
                        selectionColor: "#5a2a2a"
                        font.pixelSize: 14
                        wrapMode: TextEdit.Wrap
                        Keys.onPressed: (event) => root.handlePushKey(event)
                    }
                    Text {
                        x: 12
                        y: 10
                        visible: pushText.length === 0
                        text: "What should it do differently?"
                        color: root.faint
                        font.pixelSize: 14
                    }
                }
                property string shownKey: root.hasFront ? root.front.key : ""
                onShownKeyChanged: root.arrived()
            }

            // Nothing waiting.
            Row {
                objectName: "empty"
                visible: !root.hasFront
                height: visible ? 56 : 0
                leftPadding: 18
                spacing: 12
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    radius: 12
                    color: Qt.rgba(62 / 255, 207 / 255, 142 / 255, 0.16)
                    Text { anchors.centerIn: parent; text: "✓"; color: root.green; font.pixelSize: 13; font.weight: Font.Bold }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Nothing needs you."
                    color: root.text
                    font.pixelSize: 15
                }
            }

            // Every agent in its light: red needs you, yellow could use a
            // nudge, green is running. The card in front is the brighter one.
            Rectangle {
                width: parent.width
                visible: root.hasFront || message.text.length > 0
                         || deck.groups.running.length > 0
                height: visible ? 40 : 0
                color: "#22000000"
                Rectangle { width: parent.width; height: 1; color: "#12ffffff" }
                Row {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Light { name: "needsLight"; tint: root.needsColor; agents: deck.groups.needs }
                    Light { name: "steerLight"; tint: root.steerColor; agents: deck.groups.steer }
                }
                Text {
                    id: message
                    objectName: "message"
                    anchors.centerIn: parent
                    width: parent.width * 0.34
                    horizontalAlignment: Text.AlignHCenter
                    // Only what needs a look: a refused send, a notice, a hint.
                    // A send that went through says so by leaving.
                    text: root.hint.length > 0 ? root.hint
                          : deck.notice.length > 0 ? deck.notice
                          : (deck.message.startsWith("Not sent") || deck.message.startsWith("That agent")) ? deck.message
                          : ""
                    color: deck.message.startsWith("Not sent") && root.hint.length === 0 ? root.red : root.dim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Light {
                    name: "runningLight"
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    tint: root.runningColor
                    agents: deck.groups.running.slice(0, 8)
                }
            }
        }
    }

    // The card just answered, leaving: a frozen copy slides right for an
    // accept or a send and left for a skip, over the panel's edge.
    property var leavingCard: ({})
    Rectangle {
        id: ghost
        objectName: "ghost"
        z: 5
        x: panel.x
        // Where the card sat when it left, not where the reply line is now:
        // that height collapses as the deck empties or a wrapped draft clears,
        // which would move the copy mid-slide.
        property real restingY: panel.y + 55
        y: restingY
        width: panel.width
        height: ghostBody.implicitHeight + 30
        radius: 12
        color: Qt.rgba(26 / 255, 29 / 255, 36 / 255, 0.96)
        border.color: "#22ffffff"
        opacity: 0
        visible: opacity > 0
        property int direction: 1
        property color accent: root.green
        CardBody {
            id: ghostBody
            x: 18
            y: 14
            width: parent.width - 36
            card: root.leavingCard
        }
        transform: [
            Rotation { id: ghostTurn; origin.x: ghost.width / 2; origin.y: ghost.height; angle: 0 },
            Scale { id: ghostSize; origin.x: ghost.width / 2; origin.y: 0 },
            Translate { id: ghostShift }
        ]
        ParallelAnimation {
            id: leave
            // Off quickly, so the next card is never hidden for long. The
            // copy is drawn in this window, so it can only travel as far as
            // the room past the panel's edge on its side: farther would be
            // clipped, not leaving.
            property real room: ghost.direction < 0 ? panel.x
                                                    : root.width - panel.x - panel.width
            NumberAnimation { target: ghostShift; property: "x"; from: 0; to: ghost.direction * Math.max(8, leave.room - 8); duration: root.motion + 20; easing.type: Easing.OutQuad }
            NumberAnimation { target: ghostShift; property: "y"; from: 0; to: -8; duration: root.motion + 20; easing.type: Easing.OutCubic }
            NumberAnimation { target: ghostTurn; property: "angle"; from: 0; to: ghost.direction * 5; duration: root.motion + 20; easing.type: Easing.OutQuad }
            NumberAnimation { target: ghostSize; properties: "xScale,yScale"; from: 1; to: 0.94; duration: root.motion + 20; easing.type: Easing.OutQuad }
            NumberAnimation { target: ghost; property: "opacity"; from: 0.95; to: 0; duration: root.motion + 20; easing.type: Easing.OutQuad }
            SequentialAnimation {
                PropertyAction { target: glow; property: "border.color"; value: ghost.accent }
                NumberAnimation { target: glow; property: "opacity"; from: 0.9; to: 0; duration: 420; easing.type: Easing.OutCubic }
            }
        }
    }

    // The next card arrives: its content slides up into place while a thin
    // line of its colour fades along the top.
    ParallelAnimation {
        id: arrive
        NumberAnimation { target: arriveShift; property: "y"; from: 22; to: 0; duration: root.motion + 60; easing.type: Easing.OutCubic }
        NumberAnimation { target: frontBody; property: "opacity"; from: 0; to: 1; duration: root.motion + 60; easing.type: Easing.OutCubic }
        NumberAnimation { target: arrivalLine; property: "opacity"; from: 1; to: 0; duration: 520; easing.type: Easing.OutCubic }
    }

    Timer {
        id: flashOff
        interval: 160
        onTriggered: root.flash = ""
    }
    Timer {
        id: hintOff
        interval: 2400
        onTriggered: root.hint = ""
    }
    onHintChanged: if (hint.length > 0) hintOff.restart()

    // Holding Option alone, past a short delay so Option-typed characters
    // never read as speaking.
    Timer {
        id: holdToSpeak
        interval: 250
        // Option-Space puts the overlay away before this fires, and the
        // Option release then goes to another app: never listen while hidden.
        onTriggered: if (root.Window.window && root.Window.window.visible) deck.setListening(true)
    }

    function light(name) {
        flash = name
        flashOff.restart()
    }

    // The front card leaves in `direction` (1 right, -1 left); the deck
    // changes right after, and arrived() brings the next one in.
    function leaving(direction) {
        if (reducedMotion || !hasFront)
            return
        leavingCard = Object.assign({}, front)
        ghost.restingY = panel.y + replyLine.height + 1
        ghost.direction = direction
        ghost.accent = direction > 0 ? green : red
        leave.restart()
    }
    function arrived() {
        if (reducedMotion || settling || !hasFront)
            return
        arrivalLine.color = weightColor(front.weight)
        arrive.restart()
    }

    function openInLapis() {
        if (deck.openInLapis())
            deck.dismiss()
    }
    // In the push-back box Return is a new line; Command-Return sends it as a
    // correction and Escape goes back to the one-line reply.
    function handlePushKey(event) {
        if ((event.modifiers & Qt.ControlModifier)
                && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            const trimmed = pushText.text.trim()
            if (hasFront && front.canType && trimmed.length > 0) {
                const card = Object.assign({}, front)
                if (deck.pushBack(pushText.text)) {
                    // Sent, so it leaves the way a send leaves: right, green.
                    leavingCard = card
                    ghost.restingY = panel.y + replyLine.height + 1
                    ghost.direction = 1
                    ghost.accent = green
                    if (!reducedMotion)
                        leave.restart()
                    pushText.text = ""
                    pushing = false
                }
            }
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            const kept = pushText.text
            // The text goes back before `pushing` clears, so `typing` never
            // rests on an empty entry and the draft keeps its card.
            entry.text = kept.replace(/\n/g, " ")
            pushing = false
            event.accepted = true
        }
    }

    function handleKey(event) {
        const empty = !typing
        // macOS marks arrow keys with the keypad modifier; it is not a held key.
        const bare = (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier
        if (event.key === Qt.Key_Alt) {
            if (!event.isAutoRepeat && optionArmed) {
                optionHeld = true
                holdToSpeak.restart()
            }
            return
        }
        holdToSpeak.stop()
        hint = ""
        // Command is Qt's ControlModifier on macOS.
        if ((event.modifiers & Qt.ControlModifier)
                && (event.key === Qt.Key_BracketLeft || event.key === Qt.Key_BracketRight)) {
            deck.nextCategory(event.key === Qt.Key_BracketLeft ? -1 : 1)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier)
                   && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            if (hasFront && front.canType)
                pushing = true
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_L) {
            openInLapis()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_O) {
            if (!deck.openLink(0))
                hint = "This card has no link to open."
            event.accepted = true
        } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            if (empty && bare) {
                light("tab")
                if (front.canAccept)
                    leaving(1)
                deck.accept()
            }
            event.accepted = true
        } else if ((event.key === Qt.Key_Left || event.key === Qt.Key_Backspace)
                   && empty && bare) {
            light("left")
            leaving(-1)
            deck.skip()
            event.accepted = true
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && !entry.inputMethodComposing) {
            if (hasFront && front.canType && entry.text.trim().length > 0)
                leaving(1)
            if (deck.send(entry.text))
                entry.text = ""
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (empty)
                deck.dismiss()
            else
                entry.text = ""
            event.accepted = true
        } else if (event.text.length > 0 && !(event.modifiers & Qt.ControlModifier)) {
            light("type")
        }
    }
}
