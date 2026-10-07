import QtQuick

// Ultra Tab's overlay: a deck of agents that need you, one card in front and
// at most one peeking behind. Every card takes the same four answers: Tab
// accepts lapis's guess, holding Option speaks (a placeholder for now),
// typing anywhere then Return sends what you typed, and the left arrow skips.
// Context: `deck` (Deck), `backdrop` (draw a stand-in for the blurred desktop,
// for captures), `reducedMotion`.
Item {
    id: root
    objectName: "overlay"
    width: 1280
    height: 760

    readonly property color dim: "#8592a6"
    readonly property color text: "#dfe6f0"
    readonly property color lapisBlue: "#4a86ff"
    readonly property color gold: "#e8b931"
    readonly property color violet: "#c77dff"
    readonly property string mono: "Menlo"
    readonly property var front: deck.front
    readonly property var behind: deck.behind
    readonly property bool hasFront: front.key !== undefined
    property alias typed: entry.text

    // Captures only: what the real window's blur would show through.
    Rectangle {
        anchors.fill: parent
        visible: backdrop
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
        color: Qt.rgba(12 / 255, 17 / 255, 25 / 255, 0.72)
        border.color: "#1fffffff"
        border.width: 1
    }

    // Categories, with how many wait in each.
    Row {
        id: rail
        objectName: "rail"
        x: 26
        y: 20
        spacing: 10
        Repeater {
            model: deck.rail
            delegate: Rectangle {
                required property var modelData
                radius: 12
                height: 26
                width: pill.implicitWidth + 22
                color: modelData.selected ? "#14ffffff" : "transparent"
                Row {
                    id: pill
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        text: modelData.name
                        color: modelData.selected ? "#ffffff" : root.dim
                        font.pixelSize: 13
                    }
                    Text {
                        visible: modelData.count > 0
                        text: modelData.count
                        color: root.gold
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    // Agents at work, by name only.
    Column {
        id: side
        objectName: "running"
        anchors.right: parent.right
        anchors.rightMargin: 24
        y: 84
        width: Math.min(220, parent.width * 0.16)
        spacing: 9
        Repeater {
            model: deck.running
            delegate: Row {
                required property var modelData
                spacing: 8
                width: side.width
                Rectangle {
                    id: pulse
                    visible: !modelData.more
                    width: 8
                    height: 8
                    radius: 4
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

    Item {
        id: deckArea
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(84, parent.height * 0.18)
        width: Math.min(parent.width * 0.56, 780)
        height: parent.height - y - 64

        Text {
            objectName: "empty"
            visible: !root.hasFront
            anchors.horizontalCenter: parent.horizontalCenter
            y: 40
            text: "Nothing needs you."
            color: root.dim
            font.pixelSize: 22
        }

        // The card peeking behind the front one.
        Rectangle {
            objectName: "behindCard"
            visible: root.behind.key !== undefined
            width: parent.width
            height: frontCard.height
            y: 30
            scale: 0.95
            radius: 16
            color: "#0d131c"
            border.color: "#14ffffff"
            Text {
                x: 26
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 7
                width: parent.width - 52
                text: root.behind.name !== undefined ? root.behind.name : ""
                color: "#5b6678"
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }

        Rectangle {
            id: frontCard
            objectName: "frontCard"
            width: parent.width
            height: body.implicitHeight + 52
            radius: 16
            color: "#121a26"
            border.color: "#1affffff"
            property real fade: 1
            opacity: (root.hasFront ? 1 : 0) * fade

            property string shownKey: root.hasFront ? root.front.key : ""
            onShownKeyChanged: if (!reducedMotion && root.hasFront) arrive.restart()
            transform: Translate { id: shift }
            ParallelAnimation {
                id: arrive
                NumberAnimation { target: shift; property: "y"; from: 14; to: 0; duration: 150; easing.type: Easing.OutCubic }
                NumberAnimation { target: frontCard; property: "fade"; from: 0.4; to: 1; duration: 150 }
            }

            Column {
                id: body
                x: 26
                y: 26
                width: parent.width - 52
                spacing: 14

                Row {
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
                        color: "#5b6678"
                        font.pixelSize: 12
                    }
                }

                Text {
                    objectName: "line"
                    width: parent.width
                    text: root.hasFront ? root.front.line : ""
                    color: "#ffffff"
                    font.pixelSize: 24
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    lineHeight: 1.2
                }

                Rectangle {
                    width: parent.width
                    height: proposal.implicitHeight + 26
                    radius: 12
                    color: "#55000000"
                    border.color: "#1cffffff"
                    Text {
                        id: proposal
                        objectName: "proposal"
                        x: 16
                        y: 13
                        width: parent.width - 32
                        text: !root.hasFront ? ""
                              : root.front.request ? "A request is waiting. Answer it in lapis."
                              : root.front.proposal.length > 0 ? root.front.proposal
                              : "No guess for this one. Type your reply."
                        color: root.hasFront && root.front.proposal.length > 0 ? "#c6d2e4" : root.dim
                        font.family: root.mono
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                        maximumLineCount: 6
                        elide: Text.ElideRight
                    }
                }

                // What you are typing, or the speak placeholder.
                Item {
                    width: parent.width
                    readonly property bool active: entry.text.length > 0 || entry.inputMethodComposing
                                                   || deck.listening
                    height: active ? Math.max(entry.implicitHeight, 22) : 0
                    opacity: active ? 1 : 0
                    Text {
                        id: typedLabel
                        opacity: deck.listening ? 0 : 1
                        text: "you typed"
                        color: root.dim
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextInput {
                        id: entry
                        objectName: "entry"
                        // Hidden by opacity, never `visible`, so it keeps the keyboard.
                        opacity: deck.listening ? 0 : 1
                        x: typedLabel.implicitWidth + 10
                        width: parent.width - x
                        anchors.verticalCenter: parent.verticalCenter
                        focus: true
                        color: root.violet
                        selectionColor: "#3b2a5c"
                        font.pixelSize: 15
                        wrapMode: TextInput.Wrap
                        cursorVisible: true
                        Keys.onPressed: (event) => root.handleKey(event)
                        Keys.onReleased: (event) => {
                            if (event.key === Qt.Key_Alt && !event.isAutoRepeat) {
                                holdToSpeak.stop()
                                deck.setListening(false)
                            }
                        }
                    }
                    Row {
                        visible: deck.listening
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
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
                        Text {
                            text: "voice input comes later; type instead"
                            color: root.dim
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                Row {
                    spacing: 20
                    Repeater {
                        model: [
                            { key: "⇥", label: "accept", on: root.hasFront && root.front.canAccept },
                            { key: "⌥ hold", label: "speak", on: root.hasFront },
                            { key: "abc", label: "type", on: root.hasFront && root.front.canType },
                            { key: "←", label: "skip", on: root.hasFront }
                        ]
                        delegate: Row {
                            required property var modelData
                            spacing: 6
                            opacity: modelData.on ? 1 : 0.35
                            Rectangle {
                                width: cap.implicitWidth + 10
                                height: cap.implicitHeight + 4
                                radius: 5
                                color: "transparent"
                                border.color: "#1d3f8a"
                                Text {
                                    id: cap
                                    anchors.centerIn: parent
                                    text: modelData.key
                                    color: root.lapisBlue
                                    font.family: root.mono
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: root.dim
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        objectName: "message"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        width: parent.width * 0.6
        horizontalAlignment: Text.AlignHCenter
        text: deck.notice.length > 0 ? deck.notice : deck.message
        color: root.dim
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    // Holding Option alone, past a short delay so Option-typed characters
    // never read as speaking.
    Timer {
        id: holdToSpeak
        interval: 250
        onTriggered: deck.setListening(true)
    }

    function handleKey(event) {
        const empty = entry.text.length === 0 && !entry.inputMethodComposing
        if (event.key === Qt.Key_Alt) {
            if (!event.isAutoRepeat)
                holdToSpeak.restart()
            return
        }
        holdToSpeak.stop()
        // Command is Qt's ControlModifier on macOS.
        if ((event.modifiers & Qt.ControlModifier)
                && (event.key === Qt.Key_BracketLeft || event.key === Qt.Key_BracketRight)) {
            deck.nextCategory(event.key === Qt.Key_BracketLeft ? -1 : 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            if (empty && event.modifiers === Qt.NoModifier)
                deck.accept()
            event.accepted = true
        } else if (event.key === Qt.Key_Left && empty && event.modifiers === Qt.NoModifier) {
            deck.skip()
            event.accepted = true
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && !entry.inputMethodComposing) {
            if (deck.send(entry.text))
                entry.text = ""
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (empty)
                deck.dismiss()
            else
                entry.text = ""
            event.accepted = true
        }
    }
}
