#include "widgets/geofpview.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kViewWidth = 88;   // (7*8)*2 - (8*3)
constexpr int kViewHeight = 88;  // 11*8
constexpr int kScale = 4;

int oppositeWall(const GeoWallRecord &wall, const QString &facing)
{
    if (facing == QStringLiteral("s"))
        return wall.south;
    if (facing == QStringLiteral("n"))
        return wall.north;
    if (facing == QStringLiteral("e"))
        return wall.east;
    return wall.west;
}

int rightWall(const GeoWallRecord &wall, const QString &facing)
{
    if (facing == QStringLiteral("e"))
        return wall.south;
    if (facing == QStringLiteral("w"))
        return wall.north;
    if (facing == QStringLiteral("n"))
        return wall.east;
    return wall.west;
}

int leftWall(const GeoWallRecord &wall, const QString &facing)
{
    if (facing == QStringLiteral("w"))
        return wall.south;
    if (facing == QStringLiteral("e"))
        return wall.north;
    if (facing == QStringLiteral("s"))
        return wall.east;
    return wall.west;
}

} // namespace

GeoFirstPersonView::GeoFirstPersonView(const GeoMapRecord &map,
                                       const QVector<QImage> &wallSet,
                                       QWidget *parent)
    : QWidget(parent)
    , m_map(map)
    , m_wallSet(wallSet)
{
    // The DaxWallDef tiles use the original magenta transparency key.
    for (QImage &wall : m_wallSet) {
        wall = wall.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < wall.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(wall.scanLine(y));
            for (int x = 0; x < wall.width(); ++x) {
                if (qRed(line[x]) >= 250 && qGreen(line[x]) < 120 && qBlue(line[x]) >= 250)
                    line[x] = qRgba(0, 0, 0, 0);
            }
        }
    }

    auto *layout = new QVBoxLayout(this);

    m_view = new QLabel;
    m_view->setFixedSize(kViewWidth * kScale, kViewHeight * kScale);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setStyleSheet(QStringLiteral("background: white; border: 1px solid #3355aa;"));
    layout->addWidget(m_view, 0, Qt::AlignHCenter);

    m_status = new QLabel;
    m_status->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_status);

    auto *grid = new QGridLayout;
    auto addButton = [this, grid](const QString &text, int row, int column,
                                  void (GeoFirstPersonView::*slot)()) {
        auto *button = new QPushButton(text);
        connect(button, &QPushButton::clicked, this, slot);
        grid->addWidget(button, row, column);
    };

    addButton(QStringLiteral("Rotate left"), 0, 0, &GeoFirstPersonView::rotateLeft);
    addButton(QStringLiteral("Forward"), 0, 1, &GeoFirstPersonView::moveForward);
    addButton(QStringLiteral("Rotate right"), 0, 2, &GeoFirstPersonView::rotateRight);
    addButton(QStringLiteral("Strafe left"), 1, 0, &GeoFirstPersonView::moveLeft);
    addButton(QStringLiteral("Backward"), 1, 1, &GeoFirstPersonView::moveBackward);
    addButton(QStringLiteral("Strafe right"), 1, 2, &GeoFirstPersonView::moveRight);
    layout->addLayout(grid);

    updateView();
    updateStatus();
}

void GeoFirstPersonView::moveForward()
{
    if (m_facing == QStringLiteral("n")) --m_y;
    else if (m_facing == QStringLiteral("s")) ++m_y;
    else if (m_facing == QStringLiteral("w")) --m_x;
    else ++m_x;
    m_x = qBound(0, m_x, 15);
    m_y = qBound(0, m_y, 15);
    updateView();
    updateStatus();
    emit positionChanged(m_x, m_y, m_facing);
}

void GeoFirstPersonView::moveBackward()
{
    if (m_facing == QStringLiteral("s")) --m_y;
    else if (m_facing == QStringLiteral("n")) ++m_y;
    else if (m_facing == QStringLiteral("e")) --m_x;
    else ++m_x;
    m_x = qBound(0, m_x, 15);
    m_y = qBound(0, m_y, 15);
    updateView();
    updateStatus();
    emit positionChanged(m_x, m_y, m_facing);
}

void GeoFirstPersonView::moveLeft()
{
    if (m_facing == QStringLiteral("n")) --m_x;
    else if (m_facing == QStringLiteral("s")) ++m_x;
    else if (m_facing == QStringLiteral("w")) ++m_y;
    else --m_y;
    m_x = qBound(0, m_x, 15);
    m_y = qBound(0, m_y, 15);
    updateView();
    updateStatus();
    emit positionChanged(m_x, m_y, m_facing);
}

void GeoFirstPersonView::moveRight()
{
    if (m_facing == QStringLiteral("n")) ++m_x;
    else if (m_facing == QStringLiteral("s")) --m_x;
    else if (m_facing == QStringLiteral("w")) --m_y;
    else ++m_y;
    m_x = qBound(0, m_x, 15);
    m_y = qBound(0, m_y, 15);
    updateView();
    updateStatus();
    emit positionChanged(m_x, m_y, m_facing);
}

void GeoFirstPersonView::rotateLeft()
{
    static const QString cardinals = QStringLiteral("nwse");
    int index = cardinals.indexOf(m_facing);
    index = (index + 1) % 4;
    m_facing = cardinals.mid(index, 1);
    updateView();
    updateStatus();
    emit positionChanged(m_x, m_y, m_facing);
}

void GeoFirstPersonView::rotateRight()
{
    static const QString cardinals = QStringLiteral("nwse");
    int index = cardinals.indexOf(m_facing);
    index = (index + 3) % 4;
    m_facing = cardinals.mid(index, 1);
    updateView();
    updateStatus();
    emit positionChanged(m_x, m_y, m_facing);
}

void GeoFirstPersonView::updateView()
{
    if (m_view)
        m_view->setPixmap(QPixmap::fromImage(renderView()));
}

void GeoFirstPersonView::updateStatus()
{
    if (!m_status)
        return;
    static const QMap<QString, QString> names = {
        {QStringLiteral("n"), QStringLiteral("north")},
        {QStringLiteral("s"), QStringLiteral("south")},
        {QStringLiteral("e"), QStringLiteral("east")},
        {QStringLiteral("w"), QStringLiteral("west")},
    };
    m_status->setText(QStringLiteral("X %1  Y %2  facing %3")
                          .arg(m_x)
                          .arg(m_y)
                          .arg(names.value(m_facing, m_facing)));
}

QImage GeoFirstPersonView::wallImage(int wallType, int view) const
{
    if (m_wallSet.size() < 10)
        return {};
    const int wallTypes = m_wallSet.size() / 10;
    if (wallType < 1 || wallType > wallTypes)
        return {};
    const int index = view + (wallType - 1) * 10;
    return m_wallSet.value(index);
}

GeoWallRecord GeoFirstPersonView::mapAt(int x, int y) const
{
    for (const GeoWallRecord &wall : m_map.walls) {
        if (wall.row == y && wall.column == x)
            return wall;
    }
    GeoWallRecord empty;
    empty.row = y;
    empty.column = x;
    return empty;
}

QImage GeoFirstPersonView::renderView() const
{
    QImage image(kViewWidth, kViewHeight, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const QString &facing = m_facing;
    QVector<GeoWallRecord> nearView;
    QVector<GeoWallRecord> midView;
    QVector<GeoWallRecord> farView;

    auto row = [this](int xStart, int y, int xEnd) {
        QVector<GeoWallRecord> result;
        for (int x = xStart; x <= xEnd; ++x)
            result.append(mapAt(x, y));
        return result;
    };
    auto column = [this](int yStart, int x, int yEnd) {
        QVector<GeoWallRecord> result;
        for (int y = yStart; y <= yEnd; ++y)
            result.append(mapAt(x, y));
        return result;
    };
    auto reverse = [](QVector<GeoWallRecord> &values) {
        std::reverse(values.begin(), values.end());
    };

    if (facing == QStringLiteral("s")) {
        nearView = row(m_x - 1, m_y, m_x + 1); reverse(nearView);
        midView = row(m_x - 2, m_y + 1, m_x + 2); reverse(midView);
        farView = row(m_x - 3, m_y + 2, m_x + 3); reverse(farView);
    } else if (facing == QStringLiteral("n")) {
        nearView = row(m_x - 1, m_y, m_x + 1);
        midView = row(m_x - 2, m_y - 1, m_x + 2);
        farView = row(m_x - 3, m_y - 2, m_x + 3);
    } else if (facing == QStringLiteral("e")) {
        nearView = column(m_y - 1, m_x, m_y + 1);
        midView = column(m_y - 2, m_x + 1, m_y + 2);
        farView = column(m_y - 3, m_x + 2, m_y + 3);
    } else {
        nearView = column(m_y - 1, m_x, m_y + 1); reverse(nearView);
        midView = column(m_y - 2, m_x - 1, m_y + 2); reverse(midView);
        farView = column(m_y - 3, m_x - 2, m_y + 3); reverse(farView);
    }

    auto drawWall = [&painter, this](int wallType, int view, int x, int y) {
        const QImage wall = wallImage(wallType, view);
        if (!wall.isNull())
            painter.drawImage(QPoint(x, y), wall);
    };

    int c = 0;
    int lastWall = 0;
    int lastRightWall = 0;
    for (const GeoWallRecord &wall : farView) {
        const int wallType = oppositeWall(wall, facing);
        if (wallType > 0) {
            drawWall(wallType, 0, c * 16 - 8, 24);
            if (c <= 3 && c > 0) {
                if (leftWall(wall, facing) > 0 || lastWall > 0)
                    drawWall(wallType, 9, c * 16 - 16, 24);
            }
        }
        if (c > 3 && lastWall > 0) {
            if (wallType > 0 || lastRightWall > 0)
                drawWall(lastWall, 9, c * 16 - 16, 24);
        }
        ++c;
        lastWall = wallType;
        lastRightWall = rightWall(wall, facing);
    }

    c = 0;
    for (const GeoWallRecord &wall : farView) {
        if (c > 0 && c < 6) {
            int wallType = rightWall(wall, facing);
            if (c >= 3 && wallType > 0)
                drawWall(wallType, 2, (c == 3 ? 0 : 8) + c * 16, 16);
            wallType = leftWall(wall, facing);
            if (c <= 3 && wallType > 0)
                drawWall(wallType, 1, (c == 3 ? 0 : -8) + c * 16 - 16, 16);
        }
        ++c;
    }

    c = 0;
    for (const GeoWallRecord &wall : midView) {
        const int wallType = oppositeWall(wall, facing);
        if (wallType > 0)
            drawWall(wallType, 3, c * 24 - 16, 16);
        ++c;
    }
    c = 0;
    for (const GeoWallRecord &wall : midView) {
        if (c > 0 && c < 5) {
            int wallType = leftWall(wall, facing);
            if (wallType > 0 && c < 3)
                drawWall(wallType, 4, c * 24 - 32, 0);
            wallType = rightWall(wall, facing);
            if (wallType > 0 && c > 1)
                drawWall(wallType, 5, c * 24 + 8, 0);
        }
        ++c;
    }

    c = 0;
    for (const GeoWallRecord &wall : nearView) {
        const int wallType = oppositeWall(wall, facing);
        if (wallType > 0)
            drawWall(wallType, 6, c * 56 - 40, 0);
        ++c;
    }
    c = 0;
    for (const GeoWallRecord &wall : nearView) {
        if (c == 1) {
            int wallType = rightWall(wall, facing);
            if (wallType > 0)
                drawWall(wallType, 8, (c + 1) * 56 - 40, -8);
            wallType = leftWall(wall, facing);
            if (wallType > 0)
                drawWall(wallType, 7, c * 56 - 56, -8);
        }
        ++c;
    }

    painter.setPen(QColor(30, 90, 200));
    painter.drawRect(1, 1, kViewWidth - 2, kViewHeight - 2);
    painter.end();

    return image.scaled(kViewWidth * kScale, kViewHeight * kScale,
                        Qt::KeepAspectRatio, Qt::FastTransformation);
}
