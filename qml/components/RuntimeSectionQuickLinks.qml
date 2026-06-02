import QtQuick 2.14
import "../theme" as Design

Column {
    id: root
    property var app
    property var sections: []
    property var summarySegments: []
    property color summaryColor: Design.Theme.palette.textMuted
    property int summaryFontPixelSize: 12
    property int linkSpacing: 10
    spacing: linkSpacing
    width: parent ? parent.width : implicitWidth

    function runtimeSectionLabel() {
        return app ? app.runtimeSectionLabel.apply(app, arguments) : undefined;
    }

    function sectionKey(entry) {
        if (entry && typeof entry === "object") {
            return String(entry.key || "").trim();
        }
        return String(entry || "").trim();
    }

    function sectionLabel(entry) {
        if (entry && typeof entry === "object" && String(entry.label || "").trim().length > 0) {
            return String(entry.label || "").trim();
        }
        var key = sectionKey(entry);
        return runtimeSectionLabel(key) || ("Runtime · " + key);
    }

    function summaryText() {
        var parts = [];
        for (var i = 0; i < (summarySegments || []).length; ++i) {
            var entry = summarySegments[i] || ({ });
            var label = sectionLabel(entry);
            var description = String(entry.description || entry.text || "").trim();
            if (label.length > 0 && description.length > 0) {
                parts.push(label + " " + description);
            } else if (label.length > 0) {
                parts.push(label);
            } else if (description.length > 0) {
                parts.push(description);
            }
        }
        return parts.join(";");
    }

    Flow {
        width: parent.width
        spacing: root.linkSpacing

        Repeater {
            model: root.sections

            delegate: RuntimeSectionLinkButton {
                app: root.app
                sectionKey: root.sectionKey(modelData)
                labelOverride: root.sectionLabel(modelData)
            }
        }
    }

    Text {
        width: parent.width
        visible: text.length > 0
        text: root.summaryText()
        color: root.summaryColor
        font.pixelSize: root.summaryFontPixelSize
        wrapMode: Text.WordWrap
    }
}
