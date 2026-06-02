import QtQuick 2.14
import QtGraphicalEffects 1.14
import "theme" as Design

Rectangle {
    id: root
    width: 1480
    height: 940
    color: startupTheme.root
    clip: true
    readonly property var startupTheme: Design.Theme.startupOverlay()

    // ── Logic (preserved exactly) ─────────────────────
    property real visualProgress: Math.max(1, Number(studioBridge.startupProgress || 0))
    property string visualMessage: (studioBridge.startupMessage || "").length > 0
        ? studioBridge.startupMessage
        : "正在准备工作台 / Preparing console"
    property bool startupDismissed: false
    property bool mainLoadRequested: false
    property int minimumDisplayMs: 1200
    property double startupStartedAt: 0

    function syncStartupState() {
        visualProgress = Math.max(1, Math.min(100, Number(studioBridge.startupProgress || 0)));
        if ((studioBridge.startupMessage || "").length > 0) {
            visualMessage = studioBridge.startupMessage;
        }
        if (studioBridge.startupCanLoadMain && !mainLoadRequested && mainLoader.status === Loader.Null) {
            console.log("Startup requesting Main.qml load after startupCanLoadMain");
            mainLoadRequested = true;
        }
        if (studioBridge.startupComplete && mainLoader.status === Loader.Ready) {
            var elapsed = Math.max(0, Date.now() - startupStartedAt);
            dismissTimer.interval = Math.max(220, minimumDisplayMs - elapsed);
            console.log("Startup scheduling overlay dismiss in " + dismissTimer.interval + " ms");
            dismissTimer.restart();
        } else {
            dismissTimer.stop();
            startupDismissed = false;
        }
    }

    Timer {
        id: dismissTimer
        interval: 220
        repeat: false
        onTriggered: startupDismissed = true
    }

    Connections {
        target: studioBridge
        onStartupChanged: syncStartupState()
    }

    Behavior on visualProgress {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }

    Loader {
        id: mainLoader
        anchors.fill: parent
        asynchronous: true
        active: mainLoadRequested
        source: mainLoadRequested ? "Main.qml" : ""
        visible: startupDismissed && status === Loader.Ready
        opacity: startupDismissed ? 1 : 0

        onStatusChanged: {
            if (status === Loader.Null)    console.log("Startup mainLoader status: Null");
            else if (status === Loader.Ready)  console.log("Startup mainLoader status: Ready");
            else if (status === Loader.Loading) console.log("Startup mainLoader status: Loading");
            else if (status === Loader.Error)  console.warn("Startup mainLoader status: Error for source " + source);
        }
        onLoaded: {
            console.log("Startup mainLoader loaded Main.qml");
            studioBridge.noteMainUiLoaded();
            if (item && item.startupOverlayEnabled !== undefined) {
                item.startupOverlayEnabled = false;
            }
            syncStartupState();
        }

        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    }

    // ── Startup Overlay ───────────────────────────────
    Rectangle {
        id: startupPane
        anchors.fill: parent
        opacity: startupDismissed ? 0 : 1
        visible: opacity > 0.01

        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

        MouseArea { anchors.fill: parent; enabled: startupPane.visible }

        // ── Deep void background ─────────────────────
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0;  color: "#04050C" }
                GradientStop { position: 0.50; color: "#060810" }
                GradientStop { position: 1.0;  color: "#02030A" }
            }
        }

        // Background image with dark overlay
        Image {
            anchors.fill: parent
            source: "qrc:/app/main.jpg"
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            opacity: 0.08
        }

        // ── Hex grid overlay (scan lines) ────────────
        Item {
            anchors.fill: parent
            opacity: 0.6

            // Horizontal grid lines
            Repeater {
                model: 20
                Rectangle {
                    y: index * (parent.height / 20)
                    width: parent.width; height: 1
                    color: Design.Theme.alpha(Design.Theme.palette.accentCyan,
                        (index % 4 === 0) ? 0.06 : 0.02)
                }
            }
            // Vertical grid lines
            Repeater {
                model: 30
                Rectangle {
                    x: index * (parent.width / 30)
                    width: 1; height: parent.height
                    color: Design.Theme.alpha(Design.Theme.palette.accentCyan,
                        (index % 5 === 0) ? 0.05 : 0.015)
                }
            }
        }

        // ── Ambient glows ───────────────────────────
        // Left purple ambient
        Rectangle {
            width: 700; height: 700; radius: 350
            x: -280; y: (parent.height / 2) - 350
            color: Design.Theme.alpha(Design.Theme.palette.accentPurple, 0.10)

            SequentialAnimation on opacity {
                running: !root.startupDismissed
                loops: Animation.Infinite
                NumberAnimation { to: 0.7; duration: 3200; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 3200; easing.type: Easing.InOutSine }
            }
        }

        // Right cyan ambient
        Rectangle {
            width: 600; height: 600; radius: 300
            anchors.right: parent.right; anchors.rightMargin: -200
            y: (parent.height / 2) - 350
            color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.08)

            SequentialAnimation on opacity {
                running: !root.startupDismissed
                loops: Animation.Infinite
                NumberAnimation { to: 0.6; duration: 2800; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 2800; easing.type: Easing.InOutSine }
            }
        }

        // ── Scanning beam ────────────────────────────
        Item {
            anchors.fill: parent
            clip: true

            Rectangle {
                id: scanBeam
                width: parent.width
                height: 160
                y: -160
                gradient: Gradient {
                    GradientStop { position: 0.0;  color: "transparent" }
                    GradientStop { position: 0.5;  color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.06) }
                    GradientStop { position: 1.0;  color: "transparent" }
                }

                NumberAnimation on y {
                    running: !root.startupDismissed
                    from: -160; to: root.height + 160
                    duration: 4200
                    loops: Animation.Infinite
                    easing.type: Easing.InOutSine
                }
            }
        }

        // ── Central AI Core ──────────────────────────
        Item {
            id: aiCore
            width: 320; height: 320
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -60

            // Outer ring — breathing
            Rectangle {
                id: outerRing
                anchors.centerIn: parent
                width: 280; height: 280; radius: 140
                color: "transparent"
                border.width: 1
                border.color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.20)

                SequentialAnimation on scale {
                    running: !root.startupDismissed
                    loops: Animation.Infinite
                    NumberAnimation { to: 1.04; duration: 2000; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.00; duration: 2000; easing.type: Easing.InOutSine }
                }
                SequentialAnimation on opacity {
                    running: !root.startupDismissed
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.5; duration: 2000; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 2000; easing.type: Easing.InOutSine }
                }
            }

            // Mid ring — counter rotation
            Rectangle {
                id: midRing
                anchors.centerIn: parent
                width: 220; height: 220; radius: 110
                color: "transparent"
                border.width: 1
                border.color: Design.Theme.alpha(Design.Theme.palette.accentPurple, 0.30)

                RotationAnimation on rotation {
                    running: !root.startupDismissed
                    from: 360; to: 0
                    duration: 8000
                    loops: Animation.Infinite
                }
            }

            // Inner ring — forward rotation
            Rectangle {
                id: innerRing
                anchors.centerIn: parent
                width: 160; height: 160; radius: 80
                color: "transparent"
                border.width: 2
                border.color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.45)

                RotationAnimation on rotation {
                    running: !root.startupDismissed
                    from: 0; to: 360
                    duration: 5000
                    loops: Animation.Infinite
                }
            }

            // Progress arc track
            Rectangle {
                anchors.centerIn: parent
                width: 160; height: 160; radius: 80
                color: "transparent"
                border.width: 3
                border.color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.10)
            }

            // Core orb
            Rectangle {
                id: coreOrb
                anchors.centerIn: parent
                width: 90; height: 90; radius: 45
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#0A1C38" }
                    GradientStop { position: 1.0; color: "#040810" }
                }
                border.width: 1
                border.color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.55)

                // AI breathing glow
                layer.enabled: true
                layer.effect: Glow {
                    radius: 18; samples: 33
                    color: Design.Theme.palette.accentCyan
                    spread: 0.1

                    // We animate via a property on the parent
                }

                SequentialAnimation on opacity {
                    running: !root.startupDismissed
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.70; duration: 1600; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.00; duration: 1600; easing.type: Easing.InOutSine }
                }

                // Inner cross-hair
                Rectangle { width: 24; height: 1; color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.5); anchors.centerIn: parent }
                Rectangle { width: 1; height: 24; color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.5); anchors.centerIn: parent }
                Rectangle { width: 8; height: 8; radius: 4; anchors.centerIn: parent; color: Design.Theme.palette.accentCyan
                    SequentialAnimation on opacity {
                        running: !root.startupDismissed
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 800; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
                    }
                }
            }

            // Tick marks around the ring
            Repeater {
                model: 12
                Rectangle {
                    property real angle: index * 30 * Math.PI / 180
                    property real r: 130
                    x: aiCore.width / 2 + Math.cos(angle) * r - width / 2
                    y: aiCore.height / 2 + Math.sin(angle) * r - height / 2
                    width: (index % 3 === 0) ? 8 : 4
                    height: 1
                    radius: 0.5
                    color: Design.Theme.alpha(Design.Theme.palette.accentCyan, (index % 3 === 0) ? 0.55 : 0.20)
                    rotation: index * 30 + 90
                    transformOrigin: Item.Center
                }
            }
        }

        // ── Progress panel ───────────────────────────
        Rectangle {
            id: progressPanel
            width: 480
            height: progressColumn.implicitHeight + 28
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48

            radius: 10
            color: Qt.rgba(0.02, 0.04, 0.12, 0.78)
            border.width: 1
            border.color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.26)

            // Top neon line
            Rectangle {
                width: parent.width * 0.6; height: 1
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.50)
                layer.enabled: true
                layer.effect: Glow { radius: 4; samples: 9; color: Design.Theme.palette.accentCyan; spread: 0.3 }
            }

            // Corner accents
            Rectangle { width: 10; height: 1; color: Design.Theme.palette.accentCyan; opacity: 0.5; anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 10 }
            Rectangle { width: 1; height: 10; color: Design.Theme.palette.accentCyan; opacity: 0.5; anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 10 }
            Rectangle { width: 10; height: 1; color: Design.Theme.palette.accentCyan; opacity: 0.3; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.leftMargin: 10 }
            Rectangle { width: 1; height: 10; color: Design.Theme.palette.accentCyan; opacity: 0.3; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.leftMargin: 10 }

            Column {
                id: progressColumn
                x: 18; y: 14
                width: parent.width - 36
                spacing: 10

                // Progress bar
                Rectangle {
                    width: parent.width; height: 5; radius: 2
                    color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.08)
                    border.width: 1
                    border.color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.18)

                    Rectangle {
                        width: parent.width * visualProgress / 100.0
                        height: parent.height; radius: parent.radius
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Design.Theme.palette.accentPurple }
                            GradientStop { position: 1.0; color: Design.Theme.palette.accentCyan }
                        }

                        layer.enabled: true
                        layer.effect: Glow {
                            radius: 5; samples: 11
                            color: Design.Theme.palette.accentCyan; spread: 0.2
                        }
                    }
                }

                // Percentage
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Math.round(visualProgress) + "%"
                    color: Design.Theme.palette.accentCyan
                    font.pixelSize: 20
                    font.weight: Font.Thin
                    font.letterSpacing: 3
                }

                // Timing
                Text {
                    width: parent.width
                    text: "阶段耗时 " + Number(studioBridge.startupStepElapsedMs || 0) +
                          " ms  ·  累计 " + Number(studioBridge.startupElapsedMs || 0) + " ms"
                    horizontalAlignment: Text.AlignHCenter
                    color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.45)
                    font.pixelSize: Design.Foundation.textXs
                    font.letterSpacing: 0.5
                }

                // Message
                Text {
                    width: parent.width
                    text: visualMessage
                    horizontalAlignment: Text.AlignHCenter
                    color: Design.Theme.palette.textPrimary
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }

                // Timeline
                Column {
                    width: parent.width
                    spacing: 4
                    visible: (studioBridge.startupTimeline || []).length > 0

                    Repeater {
                        model: studioBridge.startupTimeline || []

                        delegate: Text {
                            width: parent ? parent.width : 0
                            text: String(modelData.progress || 0) + "%  ·  +" +
                                  String(modelData.stepMs || 0) + " ms  ·  Σ" +
                                  String(modelData.totalMs || 0) + " ms  ·  " +
                                  String(modelData.message || "")
                            color: (index === ((studioBridge.startupTimeline || []).length - 1))
                                ? Design.Theme.palette.accentCyan
                                : Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.35)
                            font.pixelSize: Design.Foundation.textXs
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        // ── Brand label ──────────────────────────────
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: aiCore.bottom
            anchors.topMargin: 18
            spacing: 4

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "YAOS"
                color: Design.Theme.palette.textStrong
                font.pixelSize: 28
                font.weight: Font.Black
                font.letterSpacing: 8
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "AI OPERATING SYSTEM"
                color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.60)
                font.pixelSize: Design.Foundation.textXs
                font.letterSpacing: 4
            }
        }
    }

    Component.onCompleted: {
        startupStartedAt = Date.now();
        console.log("Startup.qml component completed");
        syncStartupState();
    }
}
