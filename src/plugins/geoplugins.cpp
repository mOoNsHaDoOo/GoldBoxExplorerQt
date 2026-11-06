#include "plugins/plugins.h"

#include "core/game.h"
#include "parse/character.h"
#include "parse/ecl.h"
#include "parse/ecl_disasm.h"
#include "dax/walldef.h"
#include "widgets/geofpview.h"
#include "parse/geo.h"
#include "parse/geodax.h"
#include "widgets/characterview.h"
#include "widgets/hexview.h"

#include <QFile>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStackedWidget>
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

QString findFileCaseInsensitive(const QDir &dir, const QString &name)
{
    if (QFileInfo::exists(dir.filePath(name)))
        return dir.filePath(name);
    const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.compare(name, Qt::CaseInsensitive) == 0)
            return dir.filePath(entry);
    }
    return {};
}

QHash<int, QVector<QImage>> loadWallSets(const QString &geoPath)
{
    QHash<int, QVector<QImage>> result;
    const QDir dir = QFileInfo(geoPath).absoluteDir();
    for (int n = 0; n < 8; ++n) {
        const QString name = (n == 0) ? QStringLiteral("WALLDEF.DAX")
                                      : QStringLiteral("WALLDEF%1.DAX").arg(n);
        const QString path = findFileCaseInsensitive(dir, name);
        if (path.isEmpty())
            continue;
        DaxWallDefFile file(path);
        for (const WallSet &set : file.wallSets())
            result.insert(set.blockId, set.walls);
    }
    return result;
}

QHash<int, QVector<int>> loadWallSetMapping(const QString &geoPath)
{
    QHash<int, QVector<int>> result;
    const QFileInfo info(geoPath);
    const QDir dir = info.absoluteDir();
    const QString base = info.completeBaseName(); // GEO1, GEO
    const QString disk = base.size() > 3 ? base.mid(3) : QString();
    const QString eclName = disk.isEmpty()
                                ? QStringLiteral("ECL.DAX")
                                : QStringLiteral("ECL%1.DAX").arg(disk);
    const QString eclPath = findFileCaseInsensitive(dir, eclName);
    if (eclPath.isEmpty())
        return result;

    EclFile ecl(eclPath);
    if (!ecl.isValid())
        return result;

    for (const EclBlock &block : ecl.blocks()) {
        int geoId = -1;
        for (int i = 0; i + 8 <= block.data.size(); ++i) {
            const int value = static_cast<quint8>(block.data.at(i));
            if (value == 0x21
                && static_cast<quint8>(block.data.at(i + 1)) == 0
                && static_cast<quint8>(block.data.at(i + 3)) == 0) {
                geoId = static_cast<quint8>(block.data.at(i + 2));
            } else if (value == 0x37
                       && static_cast<quint8>(block.data.at(i + 1)) == 0
                       && static_cast<quint8>(block.data.at(i + 3)) == 0
                       && static_cast<quint8>(block.data.at(i + 5)) == 0
                       && geoId >= 0) {
                const int a = static_cast<quint8>(block.data.at(i + 2));
                const int b = static_cast<quint8>(block.data.at(i + 4));
                const int c = static_cast<quint8>(block.data.at(i + 6));
                if (!result.contains(geoId))
                    result.insert(geoId, {a, b, c});
            }
        }
    }
    return result;
}

QVector<QImage> wallSetForMap(int mapId,
                              const QHash<int, QVector<QImage>> &sets,
                              const QHash<int, QVector<int>> &mapping)
{
    if (mapping.contains(mapId)) {
        for (int id : mapping.value(mapId)) {
            if (sets.contains(id))
                return sets.value(id);
        }
    }
    if (sets.contains(mapId))
        return sets.value(mapId);
    if (!sets.isEmpty())
        return sets.constBegin().value();
    return {};
}

} // namespace

class GeoDaxView : public QTabWidget
{
public:
    explicit GeoDaxView(const GeoDaxFile &file, const QString &path, float zoom,
                       QWidget *parent = nullptr)
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

        const QHash<int, QVector<QImage>> wallSets = loadWallSets(path);
        const QHash<int, QVector<int>> mapping = loadWallSetMapping(path);

        auto *fpPage = new QWidget;
        auto *fpLayout = new QVBoxLayout(fpPage);
        auto *combo = new QComboBox;
        auto *stack = new QStackedWidget;
        for (const GeoMapRecord &map : file.maps()) {
            combo->addItem(map.name);
            auto *fp = new GeoFirstPersonView(map, wallSetForMap(map.daxId, wallSets, mapping));
            stack->addWidget(fp);
        }
        QObject::connect(combo, &QComboBox::currentIndexChanged,
                         stack, &QStackedWidget::setCurrentIndex);
        fpLayout->addWidget(combo);
        fpLayout->addWidget(stack, 1);
        addTab(fpPage, QStringLiteral("First Person"));

        setZoom(zoom);
        setCurrentIndex(count() - 1);
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
    const bool pool = parameters.filename.contains(QStringLiteral("pool"), Qt::CaseInsensitive)
                      || parameters.filename.contains(QStringLiteral("poolrad"), Qt::CaseInsensitive);
    for (const EclBlock &block : file.blocks()) {
        auto *blockTabs = new QTabWidget;

        auto *view = new QPlainTextEdit;
        QString text = QStringLiteral("Block %1 (%2 bytes)\n\n").arg(block.id).arg(block.data.size());
        text += block.strings.join(QLatin1Char('\n'));
        configureText(view, text);
        blockTabs->addTab(view, QStringLiteral("Strings"));

        const EclDisasmResult disasm = disassembleEclBlock(block.data, pool);
        auto *disasmView = new QPlainTextEdit;
        configureText(disasmView,
                      disasm.valid ? disasm.listing
                                   : QStringLiteral("Disassembly failed:\n%1")
                                         .arg(disasm.error));
        blockTabs->addTab(disasmView, QStringLiteral("Disassembly"));
        blockTabs->setCurrentIndex(1);

        tabs->addTab(blockTabs, QString::number(block.id));
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
    m_view = new GeoDaxView(file, parameters.filename, parameters.zoom);
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
