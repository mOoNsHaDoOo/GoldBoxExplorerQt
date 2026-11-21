#include "plugins/recordplugins.h"

#include "parse/record_layouts.h"
#include "widgets/hexview.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableWidget>

namespace {

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget;
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFont(QFont(QStringLiteral("Courier New"), 9));
    return table;
}

QWidget *textView(const QString &text)
{
    auto *view = new QPlainTextEdit(text);
    view->setReadOnly(true);
    view->setFont(QFont(QStringLiteral("Courier New"), 9));
    return view;
}

quint8 byteAt(const QByteArray &d, int o)
{
    return o >= 0 && o < d.size() ? static_cast<quint8>(d.at(o)) : 0;
}

quint16 be16(const QByteArray &d, int o)
{
    return o + 2 <= d.size() ? static_cast<quint16>((static_cast<quint8>(d.at(o)) << 8)
                                                    | static_cast<quint8>(d.at(o + 1)))
                             : 0;
}

QString cString(const QByteArray &d, int o, int n)
{
    return QString::fromLatin1(d.mid(o, n)).section(QChar(0), 0, 0);
}

// Several records of one layout, one tab each (named after the record).
QWidget *recordList(const char *layoutId, const QByteArray &data, const QString &about)
{
    const RecordLayouts::RecordLayout *layout = RecordLayouts::find(layoutId);
    auto *tabs = new QTabWidget;
    const int count = layout ? data.size() / layout->size : 0;
    for (int i = 0; i < count; ++i) {
        const QByteArray record = data.mid(i * layout->size, layout->size);
        QString title = QString::number(i + 1);
        const QString name = cString(record, 0, 0x2A).trimmed();
        if (layout->fields[0].count >= 8 && !name.isEmpty()
            && name.at(0).isPrint())
            title += QStringLiteral(" ") + name;
        tabs->addTab(RecordViews::fieldTable(layoutId, record), title);
    }
    tabs->addTab(textView(about), QStringLiteral("About"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

// .wiz: the $DC-byte spell block of a DKK character (character.h):
// +$00..+$49 memorized spells (bit 7: still to be learned), +$4A + n - 1
// known spell n, +$BC + group * 8 + level - 1 spell slots per group/level.
QWidget *spellBlock(const QByteArray &data)
{
    auto *table = makeTable({QStringLiteral("Offset"), QStringLiteral("Section"),
                             QStringLiteral("Index"), QStringLiteral("Value")});
    int row = 0;
    auto add = [&](int offset, const QString &section, const QString &index, const QString &value) {
        table->setRowCount(row + 1);
        table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("+$%1").arg(offset, 2, 16, QLatin1Char('0')).toUpper()));
        table->setItem(row, 1, new QTableWidgetItem(section));
        table->setItem(row, 2, new QTableWidgetItem(index));
        table->setItem(row, 3, new QTableWidgetItem(value));
        ++row;
    };
    for (int i = 0; i < 0x4A && i < data.size(); ++i) {
        const quint8 v = static_cast<quint8>(data.at(i));
        if (v)
            add(i, QStringLiteral("memorized"), QString::number(i + 1),
                QStringLiteral("spell %1%2").arg(v & 0x7F).arg(v & 0x80 ? QStringLiteral(" (to learn)") : QString()));
    }
    QStringList known;
    for (int i = 0x4A; i < 0xBC && i < data.size(); ++i) {
        if (data.at(i))
            known.append(QString::number(i - 0x4A + 1));
    }
    add(0x4A, QStringLiteral("known"), QStringLiteral("spells"), known.join(QLatin1Char(' ')));
    for (int i = 0xBC; i < 0xDC && i < data.size(); ++i) {
        const quint8 v = static_cast<quint8>(data.at(i));
        if (v)
            add(i, QStringLiteral("slots"), QStringLiteral("group %1 level %2").arg((i - 0xBC) / 8).arg((i - 0xBC) % 8 + 1),
                QString::number(v));
    }
    table->resizeColumnsToContents();
    auto *tabs = new QTabWidget;
    tabs->addTab(table, QStringLiteral("Spell block"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

// savgam?.pty (save_game, DKK H069+$40F2): resource group, state A ($400),
// B ($400), C ($200), H004+$38FC (8), previous and current game mode, four
// wall set entries (id, slot), number of characters and their file names.
QWidget *savedGame(const QByteArray &data)
{
    auto *tabs = new QTabWidget;
    int at = 0;
    QString summary;
    summary += QStringLiteral("Resource group (disk): %1\n").arg(byteAt(data, at));
    at += 1;
    const QByteArray a = data.mid(at, 0x400);
    at += 0x400;
    const QByteArray b = data.mid(at, 0x400);
    at += 0x400;
    const QByteArray c = data.mid(at, 0x200);
    at += 0x200;
    summary += QStringLiteral("Party position H004+$38FC: %1\n").arg(QString::fromLatin1(data.mid(at, 8).toHex(' ')));
    at += 8;
    summary += QStringLiteral("Previous game mode: %1\nGame mode: %2\n")
                   .arg(byteAt(data, at))
                   .arg(byteAt(data, at + 1));
    at += 2;
    for (int i = 0; i < 4; ++i, at += 4)
        summary += QStringLiteral("Wall set %1: id %2, slot %3\n").arg(i + 1).arg(be16(data, at)).arg(be16(data, at + 2));
    const int count = be16(data, at);
    at += 2;
    summary += QStringLiteral("Characters: %1\n").arg(count);
    for (int i = 0; i < count && i < 8; ++i)
        summary += QStringLiteral("  %1\n").arg(cString(data, at + i * 0x2A, 0x2A));
    summary += QStringLiteral("\n%1 of %2 bytes described.\n").arg(at + 0x150).arg(data.size());
    tabs->addTab(textView(summary), QStringLiteral("Summary"));
    tabs->addTab(RecordViews::fieldTable("dkk.stateA", a), QStringLiteral("State A"));
    tabs->addTab(RecordViews::fieldTable("dkk.stateB", b), QStringLiteral("State B"));
    tabs->addTab(RecordViews::fieldTable("dkk.stateC", c), QStringLiteral("State C"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

QWidget *globals(const QByteArray &file)
{
    QString error;
    const QByteArray image = DkkGlobals::unpack(file, &error);
    auto *tabs = new QTabWidget;
    if (image.isEmpty()) {
        tabs->addTab(textView(error), QStringLiteral("Error"));
    } else {
        tabs->addTab(RecordViews::fieldTable("dkk.globals", image), QStringLiteral("Globals"));
        tabs->addTab(new HexView(image), QStringLiteral("Unpacked"));
    }
    tabs->addTab(textView(QStringLiteral(
        "globals.daa (Death Knights of Krynn, load_globals H006+$069E):\n"
        "+$0 image size ($7FEA), +$4 offset of the Huffman tree, +$8 offset of the\n"
        "word table, +$C bit stream (H032: Huffman symbols, then run-length pairs).\n"
        "Unpacked it is the initial content of the data hunk H004; the field names\n"
        "come from h004_image.c of the C port.")), QStringLiteral("About"));
    tabs->addTab(new HexView(file), QStringLiteral("Packed"));
    return tabs;
}

} // namespace

namespace RecordViews {

QWidget *fieldTable(const char *layoutId, const QByteArray &record)
{
    const RecordLayouts::RecordLayout *layout = RecordLayouts::find(layoutId);
    if (!layout)
        return textView(QStringLiteral("Unknown layout %1").arg(QString::fromLatin1(layoutId)));
    const QVector<RecordLayouts::FieldValue> values = RecordLayouts::describe(*layout, record);
    auto *table = makeTable({QStringLiteral("Offset"), QStringLiteral("Field"), QStringLiteral("Value"),
                             QStringLiteral("Raw"), QStringLiteral("Description")});
    table->setRowCount(values.size());
    for (int i = 0; i < values.size(); ++i) {
        const RecordLayouts::FieldValue &v = values.at(i);
        table->setItem(i, 0, new QTableWidgetItem(QStringLiteral("+$%1").arg(v.offset, 3, 16, QLatin1Char('0')).toUpper()));
        table->setItem(i, 1, new QTableWidgetItem(v.name));
        table->setItem(i, 2, new QTableWidgetItem(v.value));
        table->setItem(i, 3, new QTableWidgetItem(v.raw));
        table->setItem(i, 4, new QTableWidgetItem(v.description));
    }
    table->resizeColumnsToContents();
    for (int column : {2, 3})
        table->setColumnWidth(column, qMin(table->columnWidth(column), 320));
    table->setToolTip(QStringLiteral("%1, $%2 bytes; field names from the C port of the game.")
                          .arg(QString::fromLatin1(layout->title))
                          .arg(layout->size, 0, 16));
    return table;
}

} // namespace RecordViews

QString GoldBoxRecordPlugin::kindOf(const QString &path, qint64 size)
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString name = info.fileName().toLower();
    if (name == QStringLiteral("globals.daa"))
        return QStringLiteral("dkk.globals");
    if ((suffix == QStringLiteral("gam") || suffix == QStringLiteral("pch")) && size == 0xE8)
        return QStringLiteral("dkk.character");
    if (suffix == QStringLiteral("wiz") && size == 0xDC)
        return QStringLiteral("dkk.spells");
    if (suffix == QStringLiteral("jnk") && size > 0 && size % 0x48 == 0)
        return QStringLiteral("dkk.item");
    if (suffix == QStringLiteral("eff") && size > 0 && size % 0x0A == 0)
        return QStringLiteral("dkk.effect");
    if (suffix == QStringLiteral("pty") && name.startsWith(QStringLiteral("savgam")))
        return QStringLiteral("dkk.game");
    if ((suffix == QStringLiteral("cha") || suffix == QStringLiteral("sav")) && size == 0x120)
        return QStringLiteral("por.character");
    if (suffix == QStringLiteral("itm") && size > 0 && size % 0x41 == 0)
        return QStringLiteral("por.item");
    if (suffix == QStringLiteral("spc") && size > 0 && size % 0x0A == 0)
        return QStringLiteral("por.affect");
    return {};
}

bool GoldBoxRecordPlugin::isSatisfiedBy(const QString &path) const
{
    return !kindOf(path, QFileInfo(path).size()).isEmpty();
}

QWidget *GoldBoxRecordPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return textView(QStringLiteral("Unable to open %1.").arg(parameters.filename));
    const QByteArray data = file.readAll();
    const QString kind = kindOf(parameters.filename, data.size());

    if (kind == QStringLiteral("dkk.globals"))
        return globals(data);
    if (kind == QStringLiteral("dkk.spells"))
        return spellBlock(data);
    if (kind == QStringLiteral("dkk.game"))
        return savedGame(data);
    if (kind == QStringLiteral("dkk.item"))
        return recordList("dkk.item", data, QStringLiteral(
            "Death Knights of Krynn items (.jnk, save_character H069+$1662): $48-byte\n"
            "item records as kept in memory; +$2C is the next pointer of the list."));
    if (kind == QStringLiteral("dkk.effect"))
        return recordList("dkk.effect", data, QStringLiteral(
            "Death Knights of Krynn effects (.eff, save_character H069+$1662): $0A-byte\n"
            "effect records; +$06 is the next pointer of the list."));
    if (kind == QStringLiteral("por.item"))
        return recordList("por.item", data, QStringLiteral(
            "Pool of Radiance items (.itm, save_char_to_disk in mod16): $41-byte item\n"
            "records; +$2A is the next pointer of the list."));
    if (kind == QStringLiteral("por.affect"))
        return recordList("por.affect", data, QStringLiteral(
            "Pool of Radiance affects (.spc, save_char_to_disk in mod16): 10-byte\n"
            "records; +$06 is the next pointer of the list."));

    const char *layout = kind == QStringLiteral("dkk.character") ? "dkk.character" : "por.character";
    auto *tabs = new QTabWidget;
    tabs->addTab(RecordViews::fieldTable(layout, data), QStringLiteral("Character"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}
