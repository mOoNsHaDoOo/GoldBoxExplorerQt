#pragma once

#include "parse/geodax.h"

#include <QImage>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;

// A compact first-person view modelled on the original GeoDaxFileViewer.
// It composes the pre-rendered wall faces from WALLDEF*.DAX at the same
// fixed screen positions as the upstream viewer and exposes movement,
// rotation, multi-wallset selection, and ECL event cross-linking.
class GeoFirstPersonView : public QWidget
{
    Q_OBJECT

public:
    GeoFirstPersonView(const GeoMapRecord &map,
                       const QVector<QVector<QImage>> &wallSets,
                       int eclBlockId,
                       const QVector<int> &eventTargets,
                       int eventBase = 0,
                       QWidget *parent = nullptr);

    int cursorX() const { return m_x; }
    int cursorY() const { return m_y; }
    QString facing() const { return m_facing; }

signals:
    void positionChanged(int x, int y, const QString &facing);
    void showEclRequested(int blockId, int address);

private:
    void moveForward();
    void moveBackward();
    void moveLeft();
    void moveRight();
    void rotateLeft();
    void rotateRight();
    void updateView();
    void updateStatus();

    QImage renderView() const;
    QImage wallImage(int wallType, int view) const;
    GeoWallRecord mapAt(int x, int y) const;
    int eventAddress(int event) const;

    GeoMapRecord m_map;
    QVector<QVector<QImage>> m_wallSets;
    int m_eclBlockId = -1;
    QVector<int> m_eventTargets;
    int m_eventBase = 0;
    int m_x = 8;
    int m_y = 8;
    QString m_facing = QStringLiteral("s");

    QLabel *m_view = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_eventStatus = nullptr;
    QPushButton *m_eventButton = nullptr;
};
