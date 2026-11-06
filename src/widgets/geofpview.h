#pragma once

#include "parse/geodax.h"

#include <QImage>
#include <QVector>
#include <QWidget>

class QLabel;

// A compact first-person view modelled on the original GeoDaxFileViewer.
// It composes the pre-rendered wall faces from WALLDEF*.DAX at the same
// fixed screen positions as the upstream viewer and exposes simple movement
// and rotation controls.
class GeoFirstPersonView : public QWidget
{
    Q_OBJECT

public:
    GeoFirstPersonView(const GeoMapRecord &map, const QVector<QImage> &wallSet,
                       QWidget *parent = nullptr);

    int cursorX() const { return m_x; }
    int cursorY() const { return m_y; }
    QString facing() const { return m_facing; }

signals:
    void positionChanged(int x, int y, const QString &facing);

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

    GeoMapRecord m_map;
    QVector<QImage> m_wallSet;
    int m_x = 8;
    int m_y = 8;
    QString m_facing = QStringLiteral("s");

    QLabel *m_view = nullptr;
    QLabel *m_status = nullptr;
};
