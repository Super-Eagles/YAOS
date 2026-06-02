import QtQuick 2.14

Item {
    id: root

    property int forcedColumns: 0
    property int minimumCellWidth: 280
    property int maximumColumns: 0
    property int spacing: 14

    readonly property int widthDrivenColumns: Math.max(
        1,
        Math.floor((Math.max(1, root.width) + root.spacing) /
                   Math.max(1, root.minimumCellWidth + root.spacing)))
    readonly property int columns: root.forcedColumns > 0
        ? root.forcedColumns
        : Math.max(1, root.maximumColumns > 0 ? Math.min(root.maximumColumns, root.widthDrivenColumns)
                                              : root.widthDrivenColumns)
    readonly property real cellWidth: columns > 0
        ? Math.max(root.minimumCellWidth, (root.width - ((columns - 1) * root.spacing)) / columns)
        : root.width

    default property alias contentData: contentGrid.data

    implicitHeight: contentGrid.implicitHeight
    height: implicitHeight

    Grid {
        id: contentGrid
        width: root.width
        columns: root.columns
        columnSpacing: root.spacing
        rowSpacing: root.spacing
    }
}
