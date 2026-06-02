import QtQuick 2.14

ActionButton {
    id: root
    property var app
    property string sectionKey: "overview"
    property string labelOverride: ""
    compact: true

    function normalizedSectionKey() {
        return String(sectionKey || "").trim() || "overview";
    }

    function resolvedText() {
        var overrideText = String(labelOverride || "").trim();
        if (overrideText.length > 0) {
            return overrideText;
        }
        var key = normalizedSectionKey();
        if (app && app.runtimeSectionLabel) {
            var sharedLabel = String(app.runtimeSectionLabel(key) || "").trim();
            if (sharedLabel.length > 0) {
                return sharedLabel;
            }
        }
        return "Runtime · " + key;
    }

    text: resolvedText()
    onClicked: {
        if (app && app.openRuntimeSection) {
            app.openRuntimeSection(normalizedSectionKey());
        }
    }
}
