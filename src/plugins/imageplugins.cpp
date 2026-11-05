#include "plugins/plugins.h"

#include "core/byteutils.h"
#include "dax/daximagefile.h"
#include "dax/walldef.h"
#include "tbl/tlbfile.h"
#include "widgets/hexview.h"
#include "widgets/imagesheetview.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QStringDecoder>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cstring>
#include <utility>

namespace {

bool startsWithBytes(const QByteArray &data, const char *prefix, int length)
{
    return data.size() >= length && std::memcmp(data.constData(), prefix, length) == 0;
}

QString decodeText(const QByteArray &raw)
{
    if (startsWithBytes(raw, "\xEF\xBB\xBF", 3)) {
        QStringDecoder decoder(QStringConverter::Utf8);
        return decoder.decode(raw.mid(3));
    }
    if (startsWithBytes(raw, "\xFF\xFE\x00\x00", 4)) {
        QStringDecoder decoder(QStringConverter::Utf32LE);
        return decoder.decode(raw.mid(4));
    }
    if (startsWithBytes(raw, "\x00\x00\xFE\xFF", 4)) {
        QStringDecoder decoder(QStringConverter::Utf32BE);
        return decoder.decode(raw.mid(4));
    }
    if (startsWithBytes(raw, "\xFF\xFE", 2)) {
        QStringDecoder decoder(QStringConverter::Utf16LE);
        return decoder.decode(raw.mid(2));
    }
    if (startsWithBytes(raw, "\xFE\xFF", 2)) {
        QStringDecoder decoder(QStringConverter::Utf16BE);
        return decoder.decode(raw.mid(2));
    }

    QStringDecoder decoder(QStringConverter::Utf8);
    const QString text = decoder.decode(raw);
    if (decoder.hasError())
        return QString::fromLatin1(raw);
    return text;
}

bool isProbablyText(const QByteArray &raw)
{
    if (raw.isEmpty())
        return false;

    if (startsWithBytes(raw, "\xEF\xBB\xBF", 3)
        || startsWithBytes(raw, "\xFF\xFE\x00\x00", 4)
        || startsWithBytes(raw, "\x00\x00\xFE\xFF", 4)
        || startsWithBytes(raw, "\xFF\xFE", 2)
        || startsWithBytes(raw, "\xFE\xFF", 2))
        return true;

    if (raw.contains('\0'))
        return false;

    const QString text = decodeText(raw);
    if (text.isEmpty())
        return false;

    int printable = 0;
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if (u == '\n' || u == '\r' || u == '\t' || u == '\f' || (u >= 32 && u != 0x7F))
            ++printable;
    }
    return printable >= static_cast<int>(text.size() * 0.95);
}

QWidget *makeScrollArea(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scroll->setWidget(content);
    return scroll;
}

QWidget *makeMessage(const QString &text)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(24);
    return label;
}

} // namespace

bool DaxImagePlugin::isSatisfiedBy(const QString &path) const
{
    const QString name = QFileInfo(path).fileName().toUpper();
    if (!name.endsWith(QStringLiteral(".DAX")))
        return false;
    static const char *nonImage[] = {
        "WALLDEF", "MON", "ITEM", "MUSIC", "SOUND", "ZOUNDS", "LUCK"
    };
    for (const char *prefix : nonImage) {
        if (name.startsWith(QString::fromLatin1(prefix)))
            return false;
    }
    return true;
}

QWidget *DaxImagePlugin::createViewer(const PluginParameters &parameters)
{
    m_file = std::make_unique<DaxImageFile>(parameters.filename);
    m_zoom = parameters.zoom;
    m_containerWidth = parameters.containerWidth;

    const QString name = QFileInfo(parameters.filename).fileName().toUpper();
    ImageSheetView::Options options;
    options.thirtyFivePerRow = name.startsWith(QStringLiteral("8X8D"));
    options.displayBorder = name.startsWith(QStringLiteral("SPRIT"));

    m_sheet = new ImageSheetView(m_file->bitmaps(), m_file->bitmapIds(), options);
    m_sheet->setContainerWidth(parameters.containerWidth);
    m_sheet->setZoom(parameters.zoom);
    m_scroll = qobject_cast<QScrollArea *>(makeScrollArea(m_sheet));
    return m_scroll;
}

void DaxImagePlugin::setZoom(float zoom)
{
    m_zoom = zoom;
    if (m_sheet)
        m_sheet->setZoom(zoom);
}

void DaxImagePlugin::setContainerWidth(int width)
{
    m_containerWidth = width;
    if (m_sheet)
        m_sheet->setContainerWidth(width);
}

QVector<QImage> DaxImagePlugin::getBitmaps()
{
    return m_file ? m_file->bitmaps() : QVector<QImage>{};
}

QVector<int> DaxImagePlugin::getBitmapIds()
{
    return m_file ? m_file->bitmapIds() : QVector<int>{};
}

bool DaxWallDefPlugin::isSatisfiedBy(const QString &path) const
{
    const QString name = QFileInfo(path).fileName().toUpper();
    return name.startsWith(QStringLiteral("WALL")) && name.endsWith(QStringLiteral(".DAX"));
}

QWidget *DaxWallDefPlugin::createViewer(const PluginParameters &parameters)
{
    m_file = std::make_unique<DaxWallDefFile>(parameters.filename);
    m_zoom = parameters.zoom;
    m_containerWidth = parameters.containerWidth;
    m_sheets.clear();

    auto *tabs = new QTabWidget;
    for (const WallSet &set : m_file->wallSets()) {
        ImageSheetView::Options options;
        options.rowBreakEvery = 10;
        options.rowAdvance = 96;
        options.padding = 8;
        options.startY = 32;
        auto *sheet = new ImageSheetView(set.walls, {}, options);
        sheet->setContainerWidth(parameters.containerWidth);
        sheet->setZoom(parameters.zoom);
        m_sheets.append(sheet);
        tabs->addTab(makeScrollArea(sheet), QString::number(set.blockId));
    }

    if (m_sheets.isEmpty())
        return makeMessage(QStringLiteral("No wall sets found in this WALLDEF file."));
    return tabs;
}

void DaxWallDefPlugin::setZoom(float zoom)
{
    m_zoom = zoom;
    for (ImageSheetView *sheet : std::as_const(m_sheets))
        sheet->setZoom(zoom);
}

void DaxWallDefPlugin::setContainerWidth(int width)
{
    m_containerWidth = width;
    for (ImageSheetView *sheet : std::as_const(m_sheets))
        sheet->setContainerWidth(width);
}

QVector<QImage> DaxWallDefPlugin::getBitmaps()
{
    return m_file ? m_file->allBitmaps() : QVector<QImage>{};
}

QVector<int> DaxWallDefPlugin::getBitmapIds()
{
    return m_file ? m_file->bitmapIds() : QVector<int>{};
}

bool TlbPlugin::isSatisfiedBy(const QString &path) const
{
    return QFileInfo(path).fileName().toUpper().endsWith(QStringLiteral(".TLB"));
}

QWidget *TlbPlugin::createViewer(const PluginParameters &parameters)
{
    m_file = std::make_unique<TlbFile>(parameters.filename);
    m_file->loadBitmaps();
    m_zoom = parameters.zoom;
    m_containerWidth = parameters.containerWidth;

    if (m_file->bitmaps().isEmpty())
        return makeMessage(QStringLiteral("No images could be decoded from this TLB file."));

    const QString name = QFileInfo(parameters.filename).fileName().toUpper();
    ImageSheetView::Options options;
    options.wallTlb = name.contains(QStringLiteral("8X8"));
    options.padding = 6;
    options.startY = 25;

    m_sheet = new ImageSheetView(m_file->bitmaps(), {}, options);
    m_sheet->setContainerWidth(parameters.containerWidth);
    m_sheet->setZoom(parameters.zoom);
    return makeScrollArea(m_sheet);
}

void TlbPlugin::setZoom(float zoom)
{
    m_zoom = zoom;
    if (m_sheet)
        m_sheet->setZoom(zoom);
}

void TlbPlugin::setContainerWidth(int width)
{
    m_containerWidth = width;
    if (m_sheet)
        m_sheet->setContainerWidth(width);
}

QVector<QImage> TlbPlugin::getBitmaps()
{
    return m_file ? m_file->bitmaps() : QVector<QImage>{};
}

bool TextPlugin::isSatisfiedBy(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return isProbablyText(file.readAll());
}

QWidget *TextPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return makeMessage(QStringLiteral("Unable to open the file."));

    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(decodeText(file.readAll()));
    return view;
}

bool ImagePlugin::isSatisfiedBy(const QString &path) const
{
    const QString suffix = QFileInfo(path).suffix().toUpper();
    static const char *extensions[] = {
        "PNG", "PCX", "JPG", "JPEG", "GIF", "TIF", "TIFF", "BMP", "TGA"
    };
    for (const char *extension : extensions) {
        if (suffix == QString::fromLatin1(extension))
            return true;
    }
    return false;
}

QWidget *ImagePlugin::createViewer(const PluginParameters &parameters)
{
    QImageReader reader(parameters.filename);
    m_image = reader.read();

    if (m_image.isNull()) {
        return makeMessage(QStringLiteral("Unable to load image: %1")
                               .arg(reader.errorString()));
    }

    auto *label = new QLabel;
    label->setPixmap(QPixmap::fromImage(m_image));
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return makeScrollArea(label);
}

QVector<QImage> ImagePlugin::getBitmaps()
{
    if (m_image.isNull())
        return {};
    return {m_image};
}

bool HexPlugin::isSatisfiedBy(const QString &path) const
{
    Q_UNUSED(path);
    return false;
}

QWidget *HexPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return makeMessage(QStringLiteral("Unable to open the file."));
    return new HexView(file.readAll());
}
