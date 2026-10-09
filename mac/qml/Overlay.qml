import QtQuick

// Ultra Tab's overlay: a deck of agents that need you, one card in front and
// at most one peeking behind. Every card takes the same four answers: Tab
// accepts the proposed reply, holding Option speaks (a placeholder for now),
// typing anywhere then Return sends what you typed, and the left arrow or Delete skips.
// A composed card adds a headline and up to three blocks (text, list, table,
// diagram, link). Input is never deferred for motion: the deck changes at
// once and the short slide only follows it.
// Context: `deck` (Deck), `backdrop` (draw a stand-in for the blurred desktop,
// for captures), `reducedMotion`.
Item {
    id: root
    objectName: "overlay"
    width: 1280
    height: 760

    readonly property color dim: "#8592a6"
    readonly property color faint: "#5b6678"
    readonly property color text: "#dfe6f0"
    readonly property color body: "#c6d2e4"
    readonly property color lapisBlue: "#4a86ff"
    readonly property color lapisDeep: "#1d3f8a"
    readonly property color gold: "#e8b931"
    readonly property color violet: "#c77dff"
    readonly property string mono: "Menlo"
    readonly property int motion: reducedMotion ? 0 : 180
    readonly property var front: deck.front
    readonly property var behind: deck.behind
    readonly property bool hasFront: front.key !== undefined
    readonly property bool typing: entry.text.length > 0 || entry.inputMethodComposing
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

    // Captures only: what the real window's blur would show through.
    Rectangle {
        anchors.fill: parent
        visible: backdrop
        radius: 18
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#2a2147" }
            GradientStop { position: 0.55; color: "#0e1522" }
            GradientStop { position: 1.0; color: "#0c3a46" }
        }
    }

    Rectangle {
        id: glass
        anchors.fill: parent
        radius: 18
        color: Qt.rgba(10 / 255, 14 / 255, 21 / 255, 0.74)
        border.color: "#1fffffff"
        border.width: 1
    }

    // The window moves when its background is dragged; it never resizes.
    MouseArea {
        objectName: "dragArea"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: (mouse) => {
            const window = root.Window.window
            if (window)
                window.startSystemMove()
            mouse.accepted = true
        }
    }

    component Keycap: Item {
        id: cap
        property string glyph
        property bool lit: false
        property bool available: true
        property color accent: root.lapisBlue
        property int glyphSize: 17
        property bool small: false
        readonly property int capHeight: small ? 22 : 34
        implicitWidth: Math.max(capHeight, glyphText.implicitWidth + (small ? 12 : 22))
        implicitHeight: capHeight + 2
        opacity: available ? 1 : 0.38
        // The lip under the keycap; the face sinks onto it when pressed.
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 2
            radius: cap.small ? 5 : 8
            color: cap.lit ? Qt.darker(cap.accent, 2.4) : "#07090d"
        }
        Rectangle {
            id: face
            width: parent.width
            height: cap.capHeight
            y: cap.lit ? 2 : 0
            radius: cap.small ? 5 : 8
            border.width: 1
            border.color: cap.lit ? cap.accent : "#343c4b"
            gradient: Gradient {
                GradientStop { position: 0; color: cap.lit ? Qt.darker(cap.accent, 1.9) : "#262d3a" }
                GradientStop { position: 1; color: cap.lit ? Qt.darker(cap.accent, 2.6) : "#171c25" }
            }
            Behavior on y { NumberAnimation { duration: reducedMotion ? 0 : 60 } }
            Text {
                id: glyphText
                anchors.centerIn: parent
                text: cap.glyph
                color: cap.lit ? "#ffffff" : cap.accent
                font.pixelSize: cap.glyphSize
                font.weight: Font.DemiBold
            }
        }
        // A soft glow while lit.
        Rectangle {
            anchors.fill: face
            anchors.margins: -3
            radius: face.radius + 3
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(cap.accent.r, cap.accent.g, cap.accent.b, 0.35)
            visible: cap.lit && !cap.small
        }
    }

    // Categories, with how many wait in each.
    Row {
        id: rail
        objectName: "rail"
        x: 26
        y: 20
        spacing: 8
        Repeater {
            model: deck.rail
            delegate: Rectangle {
                required property var modelData
                radius: 13
                height: 28
                width: pill.implicitWidth + 24
                color: modelData.selected ? "#16ffffff" : "transparent"
                border.color: modelData.selected ? "#22ffffff" : "transparent"
                Row {
                    id: pill
                    anchors.centerIn: parent
                    spacing: 7
                    Text {
                        text: modelData.name
                        color: modelData.selected ? "#ffffff" : root.dim
                        font.pixelSize: 14
                    }
                    Text {
                        visible: modelData.count > 0
                        text: modelData.count
                        color: root.gold
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 10
            Keycap { glyph: "⌘["; small: true; glyphSize: 11; accent: root.dim; anchors.verticalCenter: parent.verticalCenter }
            Keycap { glyph: "⌘]"; small: true; glyphSize: 11; accent: root.dim; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    // Agents at work, by name only.
    Column {
        id: side
        objectName: "running"
        anchors.right: parent.right
        anchors.rightMargin: 26
        y: 86
        width: Math.min(210, parent.width * 0.15)
        spacing: 10
        Text {
            visible: deck.running.length > 0
            text: "at work"
            color: root.faint
            font.pixelSize: 12
            font.letterSpacing: 0.6
        }
        Repeater {
            model: deck.running
            delegate: Row {
                required property var modelData
                spacing: 9
                width: side.width
                Rectangle {
                    id: pulse
                    visible: !modelData.more
                    width: 7
                    height: 7
                    radius: 3.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.lapisBlue
                    SequentialAnimation on opacity {
                        running: !reducedMotion && pulse.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 800; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
                    }
                }
                Text {
                    width: side.width - 16
                    text: modelData.name
                    color: root.dim
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }
        }
    }

    component Paragraph: Text {
        width: parent ? parent.width : 0
        color: root.body
        font.pixelSize: 15
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
                    font.pixelSize: 15
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
                small: true
                glyphSize: 11
                accent: root.dim
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

    Item {
        id: deckArea
        anchors.horizontalCenter: parent.horizontalCenter
        y: 74
        width: Math.min(parent.width * 0.58, 820)
        height: hints.y - y - 40

        Column {
            objectName: "empty"
            visible: !root.hasFront
            anchors.horizontalCenter: parent.horizontalCenter
            y: 80
            spacing: 8
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Nothing needs you."
                color: root.text
                font.pixelSize: 24
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: deck.running.length > 0 ? deck.running.length + " at work" : ""
                color: root.dim
                font.pixelSize: 14
            }
        }

        // The card peeking behind the front one.
        Rectangle {
            objectName: "behindCard"
            visible: root.behind.key !== undefined
            width: parent.width * 0.94
            x: (parent.width - width) / 2
            height: frontCard.height
            y: 26
            radius: 16
            color: "#0c121b"
            border.color: "#14ffffff"
            Text {
                x: 28
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                width: parent.width - 56
                text: root.behind.name !== undefined ? "next  ·  " + root.behind.name : ""
                textFormat: Text.PlainText
                color: root.faint
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }

        // The card just answered, leaving: accept/send slides right, skip left.
        Rectangle {
            id: ghost
            z: 2
            width: parent.width
            height: 120
            radius: 16
            color: "#121a26"
            border.color: "#1affffff"
            opacity: 0
            visible: opacity > 0
            property alias name: ghostName.text
            property alias line: ghostLine.text
            Column {
                x: 28
                y: 26
                width: parent.width - 56
                spacing: 12
                Text { id: ghostName; color: root.text; font.pixelSize: 18; font.weight: Font.DemiBold; textFormat: Text.PlainText }
                Text { id: ghostLine; width: parent.width; color: "#ffffff"; font.pixelSize: 24; font.weight: Font.Medium; elide: Text.ElideRight; textFormat: Text.PlainText }
            }
            transform: Translate { id: ghostShift }
            ParallelAnimation {
                id: leave
                property int direction: 1
                NumberAnimation { target: ghostShift; property: "x"; from: 0; to: leave.direction * 90; duration: root.motion; easing.type: Easing.OutCubic }
                NumberAnimation { target: ghost; property: "opacity"; from: 0.85; to: 0; duration: root.motion; easing.type: Easing.OutCubic }
            }
        }

        Rectangle {
            id: frontCard
            objectName: "frontCard"
            width: parent.width
            height: Math.min(body.implicitHeight + 56, deckArea.height)
            radius: 16
            color: "#121a26"
            border.color: "#1fffffff"
            clip: true
            // The slide animates its own factor so the hasFront binding stays.
            property real arriveOpacity: 1
            opacity: (root.hasFront ? 1 : 0) * arriveOpacity

            property string shownKey: root.hasFront ? root.front.key : ""
            onShownKeyChanged: if (!reducedMotion && root.hasFront) arrive.restart()
            transform: [
                Scale { id: grow; origin.x: frontCard.width / 2; origin.y: 0 },
                Translate { id: shift }
            ]
            // Comes forward from where the card behind was.
            ParallelAnimation {
                id: arrive
                NumberAnimation { target: shift; property: "y"; from: 26; to: 0; duration: root.motion; easing.type: Easing.OutCubic }
                NumberAnimation { target: grow; properties: "xScale,yScale"; from: 0.95; to: 1; duration: root.motion; easing.type: Easing.OutCubic }
                NumberAnimation { target: frontCard; property: "arriveOpacity"; from: 0.55; to: 1; duration: root.motion; easing.type: Easing.OutCubic }
            }

            Column {
                id: body
                x: 28
                y: 26
                width: parent.width - 56
                spacing: 14

                Item {
                    width: parent.width
                    height: who.implicitHeight
                    Row {
                        id: who
                        spacing: 10
                        Rectangle {
                            width: 9
                            height: 9
                            radius: 4.5
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.front.request ? root.violet : root.gold
                        }
                        Text {
                            objectName: "agentName"
                            text: root.hasFront ? root.front.name : ""
                            color: root.text
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.hasFront && root.front.folder.length > 0
                            width: folder.implicitWidth + 14
                            height: folder.implicitHeight + 4
                            radius: 5
                            color: "transparent"
                            border.color: "#22ffffff"
                            Text {
                                id: folder
                                anchors.centerIn: parent
                                text: root.hasFront ? root.front.folder : ""
                                color: root.dim
                                font.family: root.mono
                                font.pixelSize: 12
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.hasFront ? root.front.category : ""
                            color: root.faint
                            font.pixelSize: 13
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 6
                    Text {
                        objectName: "line"
                        width: parent.width
                        text: root.hasFront ? root.front.headline : ""
                        color: "#ffffff"
                        font.pixelSize: 24
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        lineHeight: 1.18
                        textFormat: Text.PlainText
                    }
                }

                Column {
                    id: blocks
                    objectName: "blocks"
                    width: parent.width
                    spacing: 14
                    visible: root.hasFront && root.front.blocks.length > 0
                    Repeater {
                        model: root.hasFront ? root.front.blocks : []
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
                    Component { id: linkBlock; Item { width: parent.width; implicitHeight: 32; LinkChip { block: parent.parent.block } } }
                }

                // The proposed reply, sent as is with Tab.
                Rectangle {
                    width: parent.width
                    height: proposal.implicitHeight + 28
                    radius: 12
                    color: "#55000000"
                    border.color: root.hasFront && root.front.canAccept ? "#3a3420" : "#1cffffff"
                    Rectangle {
                        visible: root.hasFront && root.front.canAccept
                        x: 0
                        y: 10
                        width: 3
                        height: parent.height - 20
                        radius: 1.5
                        color: root.gold
                    }
                    Text {
                        id: proposal
                        objectName: "proposal"
                        x: 18
                        y: 14
                        width: parent.width - 36
                        text: !root.hasFront ? ""
                              : root.front.request ? "A request is waiting. Answer it in the agent's own window."
                              : root.front.proposal.length > 0 ? root.front.proposal
                              : "No proposed reply for this one. Type yours."
                        color: root.hasFront && root.front.canAccept ? "#e3e9f3" : root.dim
                        font.family: root.mono
                        font.pixelSize: 15
                        lineHeight: 1.2
                        wrapMode: Text.Wrap
                        maximumLineCount: 6
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }

                // What you are typing, or the speak placeholder.
                Item {
                    width: parent.width
                    readonly property bool active: root.typing || deck.listening
                    height: active ? Math.max(entry.implicitHeight, 24) : 0
                    opacity: active ? 1 : 0
                    Text {
                        id: typedLabel
                        opacity: deck.listening ? 0 : 1
                        text: "you typed"
                        color: root.dim
                        font.pixelSize: 13
                        y: 2
                    }
                    TextInput {
                        id: entry
                        objectName: "entry"
                        // Hidden by opacity, never `visible`, so it keeps the keyboard.
                        opacity: deck.listening ? 0 : 1
                        x: typedLabel.implicitWidth + 10
                        width: parent.width - x
                        focus: true
                        color: root.violet
                        selectionColor: "#3b2a5c"
                        font.pixelSize: 16
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
                            font.pixelSize: 16
                        }
                        Text {
                            text: "voice input comes later; type instead"
                            color: root.dim
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
    }

    Text {
        objectName: "message"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: hints.top
        anchors.bottomMargin: 14
        width: parent.width * 0.6
        horizontalAlignment: Text.AlignHCenter
        text: root.hint.length > 0 ? root.hint : deck.notice.length > 0 ? deck.notice : deck.message
        color: root.dim
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    // The four answers, as keys that light up when pressed.
    Row {
        id: hints
        objectName: "hints"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 30
        Repeater {
            model: [
                { id: "tab", glyph: "⇥  tab", label: "accept", accent: "#e8b931", size: 15 },
                { id: "option", glyph: "⌥", label: "hold to speak", accent: "#c77dff", size: 18 },
                { id: "type", glyph: "abc", label: "type, ↵ to send", accent: "#4a86ff", size: 14 },
                { id: "left", glyph: "←", label: "skip", accent: "#4a86ff", size: 18 }
            ]
            delegate: Row {
                required property var modelData
                objectName: "hint-" + modelData.id
                readonly property bool on: !root.hasFront ? false
                                         : modelData.id === "tab" ? root.front.canAccept && !root.typing
                                         : modelData.id === "type" ? root.front.canType
                                         : modelData.id === "left" ? !root.typing
                                         : true
                property bool lit: !on ? false : modelData.id === "option" ? (root.optionHeld || deck.listening)
                                 : modelData.id === "type" ? (root.typing || root.flash === "type")
                                 : root.flash === modelData.id
                spacing: 10
                Keycap {
                    id: keycap
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: modelData.glyph
                    glyphSize: modelData.size
                    accent: modelData.accent
                    available: parent.on
                    lit: parent.lit
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    color: parent.lit ? "#ffffff" : parent.on ? root.body : root.faint
                    font.pixelSize: 15
                }
            }
        }
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

    // The front card leaves in `direction` while the next one comes forward.
    function leaving(direction) {
        if (reducedMotion || !hasFront)
            return
        ghost.name = front.name
        ghost.line = front.headline
        ghost.height = frontCard.height
        leave.direction = direction
        leave.restart()
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
