import QtQuick 2.14
import "../theme" as Design

Item {
    id: root

    property var iconSpec: undefined
    property color color: Design.Theme.palette.textPrimary
    property int size: root.resolvedIcon.size || Design.Theme.foundation.iconSizeMd
    readonly property var resolvedIcon: Design.Theme.resolveIcon(root.iconSpec)
    readonly property bool hasGlyph: resolvedIcon.type === "glyph" && String(resolvedIcon.value || "").length > 0
    readonly property bool hasImage: resolvedIcon.type === "image" && String(resolvedIcon.source || "").length > 0

    width: root.size
    height: root.size
    visible: root.hasGlyph || root.hasImage

    Text {
        anchors.centerIn: parent
        visible: root.hasGlyph
        text: root.resolvedIcon.value
        color: root.color
        font.family: root.resolvedIcon.fontFamily || ""
        font.pixelSize: root.size
        font.weight: Font.DemiBold
        renderType: Text.NativeRendering
    }

    Image {
        anchors.centerIn: parent
        width: root.size
        height: root.size
        visible: root.hasImage
        source: root.resolvedIcon.source
        fillMode: Image.PreserveAspectFit
        smooth: true
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
    }
}
