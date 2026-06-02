import QtQuick 2.14
Item {
    id: root

    property int forcedColumns: 0
    property int itemCount: 0
    property int minimumCellWidth: 180
    property int maximumColumns: 0
    property int columnSpacing: 10
    property int rowSpacing: columnSpacing
    default property alias contentData: contentGrid.data

    function visibleChildCount() {
        var total = 0;
        for (var i = 0; i < contentGrid.children.length; ++i) {
            var child = contentGrid.children[i];
            if (!child) {
                continue;
            }
            if (child.visible === false) {
                continue;
            }
            ++total;
        }
        return total;
    }

    readonly property int resolvedItemCount: Math.max(1, root.itemCount > 0 ? root.itemCount : root.visibleChildCount())
    readonly property int widthDrivenColumns: Math.max(
        1,
        Math.floor((Math.max(0, root.width) + root.columnSpacing) /
                   Math.max(1, root.minimumCellWidth + root.columnSpacing)))
    readonly property int columns: root.forcedColumns > 0
        ? root.forcedColumns
        : Math.max(
            1,
            Math.min(root.maximumColumns > 0 ? root.maximumColumns : root.resolvedItemCount,
                     root.resolvedItemCount,
                     root.widthDrivenColumns))
    readonly property real cellWidth: root.columns > 0
        ? Math.max(0, (root.width - ((root.columns - 1) * root.columnSpacing)) / root.columns)
        : root.width

    implicitHeight: contentGrid.implicitHeight
    height: implicitHeight

    function reflowChildren() {
        for (var i = 0; i < contentGrid.children.length; ++i) {
            var child = contentGrid.children[i];
            if (!child || child === spacer) {
                continue;
            }
            child.width = root.cellWidth;
        }
    }

    function scheduleReflow() {
        Qt.callLater(root.reflowChildren);
    }

    onWidthChanged: root.scheduleReflow()
    onColumnsChanged: root.scheduleReflow()

    Grid {
        id: contentGrid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        columns: root.columns
        columnSpacing: root.columnSpacing
        rowSpacing: root.rowSpacing

        onChildrenChanged: root.scheduleReflow()

        Item {
            id: spacer
            visible: false
            width: 0
            height: 0
        }
    }

    Component.onCompleted: {
        root.scheduleReflow()
    }
}
