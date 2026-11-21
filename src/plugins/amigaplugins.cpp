#include "plugins/amigaplugins.h"

#include "widgets/hexview.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

quint32 be32(const QByteArray &d, qint64 o)
{
    if (o < 0 || o + 4 > d.size())
        return 0;
    return (static_cast<quint32>(static_cast<quint8>(d.at(o))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(d.at(o + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(d.at(o + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(d.at(o + 3)));
}

int be16(const QByteArray &d, qint64 o)
{
    if (o < 0 || o + 2 > d.size())
        return 0;
    return (static_cast<quint8>(d.at(o)) << 8) | static_cast<quint8>(d.at(o + 1));
}

bool isIcon(const QByteArray &d)
{
    return d.size() >= 78 && be16(d, 0) == 0xE310 && be16(d, 2) == 1;
}

bool isHunkExecutable(const QByteArray &d)
{
    return d.size() >= 24 && be32(d, 0) == 0x3F3;
}

QPlainTextEdit *textView(const QString &text)
{
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(text);
    return view;
}

// Workbench 1.x pens; deeper icons get extra greys (their real colours
// depend on the user's Workbench palette).
QRgb iconPen(int index)
{
    static const QRgb pens[8] = {qRgb(0x00, 0x55, 0xAA), qRgb(0xFF, 0xFF, 0xFF),
                                 qRgb(0x00, 0x00, 0x22), qRgb(0xFF, 0x88, 0x00),
                                 qRgb(0x99, 0x99, 0x99), qRgb(0xBB, 0xBB, 0xBB),
                                 qRgb(0x66, 0x66, 0x66), qRgb(0x44, 0x44, 0x44)};
    return pens[index & 7];
}

// Reads an Intuition Image structure and its planes at offset; returns the
// offset after the data, or -1.
qint64 readIconImage(const QByteArray &d, qint64 at, QImage &image, QString &info)
{
    if (at + 20 > d.size())
        return -1;
    const int width = be16(d, at + 4);
    const int height = be16(d, at + 6);
    const int depth = be16(d, at + 8);
    const int planePick = static_cast<quint8>(d.at(at + 14));
    const int planeOnOff = static_cast<quint8>(d.at(at + 15));
    if (width <= 0 || height <= 0 || depth <= 0 || depth > 8 || width > 2000 || height > 2000)
        return -1;
    const int rowBytes = ((width + 15) / 16) * 2;
    const qint64 data = at + 20;
    const qint64 end = data + static_cast<qint64>(rowBytes) * height * depth;
    if (end > d.size())
        return -1;
    image = QImage(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int pen = 0;
            int plane = 0;
            for (int bit = 0; bit < 8; ++bit) {
                if (planePick & (1 << bit)) {
                    if (plane < depth) {
                        const qint64 o = data + (static_cast<qint64>(plane) * height + y) * rowBytes + x / 8;
                        if ((static_cast<quint8>(d.at(o)) >> (7 - (x & 7))) & 1)
                            pen |= 1 << bit;
                    }
                    ++plane;
                } else if (planeOnOff & (1 << bit)) {
                    pen |= 1 << bit;
                }
            }
            image.setPixel(x, y, iconPen(pen));
        }
    }
    info += QStringLiteral("  image %1x%2, depth %3, PlanePick $%4, PlaneOnOff $%5\n")
                .arg(width).arg(height).arg(depth)
                .arg(planePick, 2, 16, QLatin1Char('0')).arg(planeOnOff, 2, 16, QLatin1Char('0'));
    return end;
}

QString readIconString(const QByteArray &d, qint64 &at)
{
    const quint32 length = be32(d, at);
    at += 4;
    if (length == 0 || at + length > static_cast<quint32>(d.size()))
        return {};
    QByteArray text = d.mid(static_cast<int>(at), static_cast<int>(length));
    at += length;
    const int nul = text.indexOf('\0');
    if (nul >= 0)
        text.truncate(nul);
    return QString::fromLatin1(text);
}

QString hunkName(quint32 type)
{
    switch (type & 0x3FFFFFFF) {
    case 0x3E9: return QStringLiteral("CODE");
    case 0x3EA: return QStringLiteral("DATA");
    case 0x3EB: return QStringLiteral("BSS");
    case 0x3EC: return QStringLiteral("RELOC32");
    case 0x3F0: return QStringLiteral("SYMBOL");
    case 0x3F1: return QStringLiteral("DEBUG");
    case 0x3F2: return QStringLiteral("END");
    default: return QStringLiteral("$%1").arg(type, 0, 16);
    }
}

QString describeHunks(const QByteArray &d)
{
    QString text = QStringLiteral("AmigaOS hunk executable (HUNK_HEADER)\nSize: %1 bytes\n\n").arg(d.size());
    qint64 at = 4;
    // Resident library names (normally none).
    while (at + 4 <= d.size()) {
        const quint32 longs = be32(d, at);
        at += 4;
        if (longs == 0)
            break;
        at += static_cast<qint64>(longs) * 4;
    }
    const quint32 tableSize = be32(d, at);
    const quint32 first = be32(d, at + 4);
    const quint32 last = be32(d, at + 8);
    at += 12;
    text += QStringLiteral("Hunks: %1 (first %2, last %3)\n").arg(tableSize).arg(first).arg(last);
    if (last < first || last - first > 4096)
        return text + QStringLiteral("Invalid hunk table.\n");
    for (quint32 i = first; i <= last; ++i) {
        const quint32 size = be32(d, at);
        at += 4;
        const char *memory = (size >> 30) == 1 ? " CHIP" : (size >> 30) == 2 ? " FAST" : "";
        text += QStringLiteral("  hunk %1: %2 bytes%3\n").arg(i).arg((size & 0x3FFFFFFF) * 4).arg(QString::fromLatin1(memory));
    }
    text += QStringLiteral("\nBlocks:\n");
    int index = 0;
    int guard = 0;
    while (at + 4 <= d.size() && guard++ < 10000) {
        const quint32 type = be32(d, at) & 0x3FFFFFFF;
        at += 4;
        if (type == 0x3E9 || type == 0x3EA || type == 0x3F1) {
            const quint32 longs = be32(d, at) & 0x3FFFFFFF;
            text += QStringLiteral("  %1 %2 bytes\n").arg(hunkName(type), -8).arg(longs * 4);
            at += 4 + static_cast<qint64>(longs) * 4;
        } else if (type == 0x3EB) {
            text += QStringLiteral("  %1 %2 bytes\n").arg(hunkName(type), -8).arg((be32(d, at) & 0x3FFFFFFF) * 4);
            at += 4;
        } else if (type == 0x3EC) {
            int relocs = 0;
            while (at + 4 <= d.size()) {
                const quint32 count = be32(d, at);
                at += 4;
                if (count == 0)
                    break;
                at += 4 + static_cast<qint64>(count) * 4;
                relocs += static_cast<int>(count);
            }
            text += QStringLiteral("  RELOC32  %1 entries\n").arg(relocs);
        } else if (type == 0x3F0) {
            int symbols = 0;
            while (at + 4 <= d.size()) {
                const quint32 longs = be32(d, at);
                at += 4;
                if (longs == 0)
                    break;
                at += static_cast<qint64>(longs) * 4 + 4;
                ++symbols;
            }
            text += QStringLiteral("  SYMBOL   %1 symbols\n").arg(symbols);
        } else if (type == 0x3F2) {
            text += QStringLiteral("  END      (end of hunk %1)\n").arg(index++);
        } else {
            text += QStringLiteral("  unknown block type $%1 at %2, stopping\n").arg(type, 0, 16).arg(at - 4);
            break;
        }
    }
    text += QStringLiteral("\nSystem/tool executable: the explorer does not disassemble 68000 code.\n");
    return text;
}

} // namespace

bool AmigaSystemPlugin::isSatisfiedBy(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(24);
    if (QFileInfo(path).suffix().compare(QStringLiteral("info"), Qt::CaseInsensitive) == 0
        && head.size() >= 4 && be16(head, 0) == 0xE310)
        return true;
    return isHunkExecutable(head);
}

QWidget *AmigaSystemPlugin::createViewer(const PluginParameters &parameters)
{
    m_images.clear();
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return textView(QStringLiteral("Unable to open the file."));
    const QByteArray d = file.readAll();
    auto *tabs = new QTabWidget;

    if (isHunkExecutable(d)) {
        tabs->addTab(textView(describeHunks(d)), QStringLiteral("Hunks"));
        tabs->addTab(new HexView(d), QStringLiteral("Hex"));
        return tabs;
    }

    if (!isIcon(d)) {
        tabs->addTab(new HexView(d), QStringLiteral("Hex"));
        return tabs;
    }

    static const char *types[] = {"?", "Disk", "Drawer", "Tool", "Project", "Garbage", "Device", "Kick", "AppIcon"};
    const int type = static_cast<quint8>(d.at(48));
    QString info = QStringLiteral("Workbench icon (DiskObject)\n");
    info += QStringLiteral("Type: %1 (%2)\n").arg(type).arg(QString::fromLatin1(types[type <= 8 ? type : 0]));
    info += QStringLiteral("Gadget: %1x%2, flags $%3\n").arg(be16(d, 12)).arg(be16(d, 14)).arg(be16(d, 16), 4, 16, QLatin1Char('0'));
    info += QStringLiteral("Position: %1,%2  stack %3\n")
                .arg(static_cast<qint32>(be32(d, 58))).arg(static_cast<qint32>(be32(d, 62))).arg(be32(d, 74));

    qint64 at = 78;
    if (be32(d, 66) != 0)
        at += 56; // DrawerData (NewWindow + position)
    QImage normal;
    QImage selected;
    if (be32(d, 22) != 0)
        at = readIconImage(d, at, normal, info);
    if (at > 0 && be32(d, 26) != 0)
        at = readIconImage(d, at, selected, info);
    if (at > 0 && be32(d, 50) != 0)
        info += QStringLiteral("Default tool: %1\n").arg(readIconString(d, at));
    if (at > 0 && be32(d, 54) != 0 && at + 4 <= d.size()) {
        const quint32 entries = be32(d, at) / 4;
        at += 4;
        info += QStringLiteral("Tool types:\n");
        for (quint32 i = 1; i < entries && at > 0 && at < d.size(); ++i)
            info += QStringLiteral("  %1\n").arg(readIconString(d, at));
    }
    info += QStringLiteral("\nColours: Workbench 1.x pens (blue, white, black, orange);\n"
                           "the real colours come from the Workbench palette.\n");

    auto *imageBox = new QWidget;
    auto *layout = new QVBoxLayout(imageBox);
    for (const QImage &image : {normal, selected}) {
        if (image.isNull())
            continue;
        m_images.append(image);
        auto *label = new QLabel;
        label->setPixmap(QPixmap::fromImage(image.scaled(image.width() * 2, image.height() * 2)));
        layout->addWidget(label);
    }
    layout->addStretch(1);
    auto *scroll = new QScrollArea;
    scroll->setWidget(imageBox);
    scroll->setWidgetResizable(true);
    tabs->addTab(scroll, QStringLiteral("Icon"));
    tabs->addTab(textView(info), QStringLiteral("Info"));
    tabs->addTab(new HexView(d), QStringLiteral("Hex"));
    return tabs;
}
