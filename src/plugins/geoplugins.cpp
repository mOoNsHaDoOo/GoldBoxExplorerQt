#include "plugins/plugins.h"

#include "core/game.h"
#include "parse/character.h"
#include "parse/ecl.h"
#include "parse/geo.h"
#include "parse/geodax.h"
#include "widgets/characterview.h"
#include "widgets/hexview.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <utility>

namespace {

void configureText(QPlainTextEdit *view, const QString &text)
{
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(text);
}

class GeoMapWidget : public QWidget
{
public:
    explicit GeoMapWidget(const GeoMapRecord &map, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_map(map)
    {
        setZoom(1.0f);
    }

    void setZoom(float zoom)
    {
        m_zoom = qMax(0.1f, zoom);
        setFixedSize(qRound(fullSize * m_zoom), qRound(fullSize * m_zoom));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        painter.scale(m_zoom, m_zoom);

        const QFont gutterFont(QStringLiteral("Courier New"), 14);
        painter.setFont(gutterFont);
        painter.setPen(QColor(85, 85, 85));
        for (int i = 0; i < 16; ++i) {
            const QRectF rowRect(0, i * gutterSize + gutterSize, roomSize, roomSize);
            painter.drawText(rowRect, Qt::AlignCenter, QString::number(i));
            const QRectF colRect(i * gutterSize + gutterSize, 0, roomSize, roomSize);
            painter.drawText(colRect, Qt::AlignCenter, QString::number(i));
        }

        const QColor wallColor(170, 170, 170);
        const QFont eventFont(QStringLiteral("Courier New"), 12);

        for (const GeoWallRecord &wall : m_map.walls) {
            const int x = wall.column * roomSize + gutterSize;
            const int y = wall.row * roomSize + gutterSize;

            painter.fillRect(x, y, roomSize, roomSize, QColor(85, 85, 85));

            if (wall.event > 0) {
                painter.fillRect(x, y, roomSize, roomSize, QColor(34, 139, 34));
                if ((wall.event & 127) > 0) {
                    painter.setFont(eventFont);
                    painter.setPen(Qt::black);
                    painter.drawText(QPointF(x + 1, y + 13), QString::number(wall.event & 127));
                }
            }

            painter.fillRect(x, y, roomSize, wallThickness, wallColor);
            painter.fillRect(x + roomSize - wallThickness, y, wallThickness, roomSize, wallColor);
            painter.fillRect(x, y + roomSize - wallThickness, roomSize, wallThickness, wallColor);
            painter.fillRect(x, y, wallThickness, roomSize, wallColor);

            const QColor doorColors[4] = {
                QColor(0, 0, 0, 0), Qt::white, Qt::yellow, Qt::red
            };
            const int northDoor = wall.door & 3;
            const int eastDoor = (wall.door >> 2) & 3;
            const int southDoor = (wall.door >> 4) & 3;
            const int westDoor = (wall.door >> 6) & 3;

            if (northDoor > 0) {
                painter.fillRect(x + wallThickness * 2, y, roomSize - wallThickness * 4,
                                 wallThickness * 2, doorColors[northDoor]);
            }
            if (eastDoor > 0) {
                painter.fillRect(x + roomSize - wallThickness * 2, y + wallThickness * 2,
                                 wallThickness * 2, roomSize - wallThickness * 4,
                                 doorColors[eastDoor]);
            }
            if (southDoor > 0) {
                painter.fillRect(x + wallThickness * 2, y + roomSize - wallThickness * 2,
                                 roomSize - wallThickness * 4, wallThickness * 2,
                                 doorColors[southDoor]);
            }
            if (westDoor > 0) {
                painter.fillRect(x, y + wallThickness * 2, wallThickness * 2,
                                 roomSize - wallThickness * 4, doorColors[westDoor]);
            }
        }
    }

private:
    static constexpr int roomSize = 32;
    static constexpr int wallThickness = 2;
    static constexpr int baseMapSize = 512;
    static constexpr int gutterSize = 32;
    static constexpr int fullSize = baseMapSize + gutterSize;

    GeoMapRecord m_map;
    float m_zoom = 1.0f;
};

} // namespace

class GeoDaxView : public QTabWidget
{
public:
    explicit GeoDaxView(const GeoDaxFile &file, float zoom, QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        for (const GeoMapRecord &map : file.maps()) {
            auto *widget = new GeoMapWidget(map);
            auto *scroll = new QScrollArea;
            scroll->setWidgetResizable(false);
            scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            scroll->setWidget(widget);
            m_maps.append(widget);
            addTab(scroll, map.name);
        }
        setZoom(zoom);
    }

    void setZoom(float zoom)
    {
        for (GeoMapWidget *map : std::as_const(m_maps))
            map->setZoom(zoom);
    }

private:
    QVector<GeoMapWidget *> m_maps;
};

bool CharacterPlugin::isSatisfiedBy(const QString &path) const
{
    if (GameDetector::determineGameFrom(path) != GameList::PoolOfRadiance)
        return false;

    const QString name = QFileInfo(path).fileName().toUpper();
    return name.endsWith(QStringLiteral(".SAV"))
           || name.startsWith(QStringLiteral("ITEMS"))
           || name.endsWith(QStringLiteral(".ITM"))
           || name.endsWith(QStringLiteral(".SPC"))
           || (name.endsWith(QStringLiteral(".DAX"))
               && (name.contains(QStringLiteral("MON")) || name.startsWith(QStringLiteral("ITEM"))));
}

QWidget *CharacterPlugin::createViewer(const PluginParameters &parameters)
{
    CharacterDocument document;
    QString error;
    if (!loadCharacterDocument(parameters.filename, document, &error)) {
        auto *view = new QPlainTextEdit;
        configureText(view, QStringLiteral("Unable to read the character file:\n%1").arg(error));
        return view;
    }
    return new CharacterView(document);
}

bool GeoPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    const QString name = info.fileName().toUpper();
    return name.startsWith(QStringLiteral("GEO"))
           && info.suffix().toUpper() == QStringLiteral("DAT");
}

QWidget *GeoPlugin::createViewer(const PluginParameters &parameters)
{
    GeoFile file(parameters.filename);
    auto *tabs = new QTabWidget;

    auto *summary = new QPlainTextEdit;
    configureText(summary, file.summary());
    tabs->addTab(summary, QStringLiteral("Summary"));

    auto *map = new QPlainTextEdit;
    configureText(map, file.map());
    tabs->addTab(map, QStringLiteral("Map"));

    return tabs;
}

bool EclPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    const QString name = info.fileName().toUpper();
    return name.startsWith(QStringLiteral("ECL"))
           && info.suffix().toUpper() == QStringLiteral("DAX");
}

QWidget *EclPlugin::createViewer(const PluginParameters &parameters)
{
    EclFile file(parameters.filename);
    if (!file.isValid()) {
        auto *view = new QPlainTextEdit;
        configureText(view, QStringLiteral("Unable to read the ECL file:\n%1").arg(file.errorString()));
        return view;
    }

    auto *tabs = new QTabWidget;
    for (const EclBlock &block : file.blocks()) {
        auto *view = new QPlainTextEdit;
        QString text = QStringLiteral("Block %1 (%2 bytes)\n\n").arg(block.id).arg(block.data.size());
        text += block.strings.join(QLatin1Char('\n'));
        configureText(view, text);
        tabs->addTab(view, QString::number(block.id));
    }

    auto *all = new QPlainTextEdit;
    configureText(all, file.allText());
    tabs->addTab(all, QStringLiteral("ECL Text"));

    QFile raw(parameters.filename);
    if (raw.open(QIODevice::ReadOnly))
        tabs->addTab(new HexView(raw.readAll()), QStringLiteral("Hex"));

    return tabs;
}

bool GeoDaxPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    const QString name = info.fileName().toUpper();
    return name.startsWith(QStringLiteral("GEO"))
           && info.suffix().toUpper() == QStringLiteral("DAX");
}

QWidget *GeoDaxPlugin::createViewer(const PluginParameters &parameters)
{
    GeoDaxFile file(parameters.filename);
    if (!file.isValid()) {
        auto *view = new QPlainTextEdit;
        configureText(view, QStringLiteral("Unable to read the map file:\n%1").arg(file.errorString()));
        return view;
    }

    m_zoom = parameters.zoom;
    m_view = new GeoDaxView(file, parameters.zoom);
    return m_view;
}

void GeoDaxPlugin::setZoom(float zoom)
{
    m_zoom = zoom;
    if (m_view)
        m_view->setZoom(zoom);
}

void GeoDaxPlugin::setContainerWidth(int width)
{
    Q_UNUSED(width);
}
