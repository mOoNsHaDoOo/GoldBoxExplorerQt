#include "plugins/krynnviews.h"

#include "core/daxfile.h"
#include "parse/krynn_records.h"
#include "widgets/hexview.h"

#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableWidget>

namespace KrynnViews {

namespace {

using KrynnFields::Kind;

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget;
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setFont(QFont(QStringLiteral("Courier New"), 9));
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

QTableWidgetItem *item(const QString &text)
{
    return new QTableWidgetItem(text);
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

quint8 byteAt(const QByteArray &d, int o)
{
    return (o >= 0 && o < d.size()) ? static_cast<quint8>(d.at(o)) : 0;
}

int be16(const QByteArray &d, int o)
{
    return (byteAt(d, o) << 8) | byteAt(d, o + 1);
}

QString hex(const QByteArray &d)
{
    return QString::fromLatin1(d.toHex(' ').toUpper());
}

const QString kNote = QStringLiteral(
    "Field names come from the readable C port of the Amiga Champions of Krynn\n"
    "executable (tools/gen_krynn_fields.py). Offsets are those of the in-memory\n"
    "record; DAX records are converted the way Load_MonsterRecord does.");

QWidget *itemRecords(const QByteArray &block, bool memoryLayout)
{
    const int size = memoryLayout ? KrynnRecords::itemSize : KrynnRecords::pcItemSize;
    const int count = block.size() / size;
    auto *tabs = new QTabWidget;

    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Name text"),
                             QStringLiteral("Name words"), QStringLiteral("Type (+$30)"),
                             QStringLiteral("Bonus"), QStringLiteral("Qty"),
                             QStringLiteral("Weight"), QStringLiteral("Value"),
                             QStringLiteral("Charges"), QStringLiteral("Cursed")});
    table->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        const QByteArray record = block.mid(i * size, size);
        const QByteArray mem = memoryLayout ? record : KrynnRecords::itemToMemory(record);
        QByteArray text = mem.left(0x28);
        if (!text.isEmpty() && byteAt(text, 0) < 0x20)
            text = text.mid(1, byteAt(text, 0)); // length-prefixed name
        const int end = text.indexOf('\0');
        if (end >= 0)
            text.truncate(end);
        text = text.trimmed();
        table->setItem(i, 0, item(QString::number(i)));
        table->setItem(i, 1, item(QString::fromLatin1(text)));
        table->setItem(i, 2, item(KrynnRecords::itemName(mem)));
        table->setItem(i, 3, item(QStringLiteral("%1 %2").arg(byteAt(mem, 0x30))
                                      .arg(KrynnRecords::itemWordName(byteAt(mem, 0x30)))));
        table->setItem(i, 4, item(QString::number(static_cast<qint16>(be16(mem, 0x36)))));
        table->setItem(i, 5, item(QString::number(byteAt(mem, 0x40))));
        table->setItem(i, 6, item(QString::number(be16(mem, 0x3E))));
        table->setItem(i, 7, item(QString::number(be16(mem, 0x42))));
        table->setItem(i, 8, item(QString::number(byteAt(mem, 0x44))));
        table->setItem(i, 9, item(QString::number(byteAt(mem, 0x3C))));
    }
    table->resizeColumnsToContents();
    tabs->addTab(table, QStringLiteral("Items"));

    for (int i = 0; i < count && i < 64; ++i) {
        const QByteArray record = block.mid(i * size, size);
        const QByteArray mem = memoryLayout ? record : KrynnRecords::itemToMemory(record);
        tabs->addTab(fieldTable(Kind::Item, mem), QString::number(i));
    }
    return tabs;
}

QWidget *effectRecords(const QByteArray &block, bool memoryLayout)
{
    const int size = memoryLayout ? KrynnRecords::effectSize : KrynnRecords::pcEffectSize;
    const int count = block.size() / size;
    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Type (+$00)"),
                             QStringLiteral("Duration (+$02)"), QStringLiteral("Parameter (+$04)"),
                             QStringLiteral("Flag (+$05)"), QStringLiteral("Raw")});
    table->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        const QByteArray record = block.mid(i * size, size);
        const QByteArray mem = memoryLayout ? record : KrynnRecords::effectToMemory(record);
        table->setItem(i, 0, item(QString::number(i)));
        table->setItem(i, 1, item(QStringLiteral("%1 ($%2)").arg(byteAt(mem, 0))
                                      .arg(byteAt(mem, 0), 2, 16, QLatin1Char('0'))));
        const int duration = be16(mem, 2);
        table->setItem(i, 2, item(duration == 0 ? QStringLiteral("0 (permanent)")
                                                : QString::number(duration)));
        table->setItem(i, 3, item(QString::number(byteAt(mem, 4))));
        table->setItem(i, 4, item(QString::number(byteAt(mem, 5))));
        table->setItem(i, 5, item(hex(record)));
    }
    table->resizeColumnsToContents();
    return table;
}

} // namespace

QWidget *fieldTable(KrynnFields::Kind kind, const QByteArray &memory)
{
    const QVector<KrynnRecords::FieldValue> values = KrynnRecords::describe(kind, memory);
    auto *table = makeTable({QStringLiteral("Offset"), QStringLiteral("Field"),
                             QStringLiteral("Value"), QStringLiteral("Raw")});
    table->setRowCount(values.size());
    for (int i = 0; i < values.size(); ++i) {
        const KrynnRecords::FieldValue &v = values.at(i);
        table->setItem(i, 0, item(QStringLiteral("+$%1").arg(v.offset, 3, 16, QLatin1Char('0')).toUpper()));
        table->setItem(i, 1, item(v.name));
        table->setItem(i, 2, item(v.value));
        table->setItem(i, 3, item(v.raw));
    }
    table->resizeColumnsToContents();
    table->setColumnWidth(1, qMin(table->columnWidth(1), 460));
    table->setToolTip(kNote);
    return table;
}

QWidget *daxRecords(const DaxFile &dax, const QString &fileName)
{
    const QString name = QFileInfo(fileName).fileName().toUpper();
    auto *tabs = new QTabWidget;
    for (const DaxFileBlock &block : dax.blocks()) {
        QWidget *view = nullptr;
        if (name.contains(QStringLiteral("CHA"))) {
            auto *inner = new QTabWidget;
            const QByteArray mem = KrynnRecords::monsterToMemory(block.data);
            inner->addTab(fieldTable(Kind::Character, mem), QStringLiteral("Monster"));
            inner->addTab(new HexView(block.data), QStringLiteral("PC record"));
            view = inner;
        } else if (name.contains(QStringLiteral("SPC"))) {
            view = effectRecords(block.data, false);
        } else {
            view = itemRecords(block.data, false);
        }
        QString title = QString::number(block.id);
        if (name.contains(QStringLiteral("CHA")))
            title += QStringLiteral(" ") + QString::fromLatin1(block.data.mid(1, 15)).section(QChar(0), 0, 0);
        tabs->addTab(view, title);
    }
    tabs->addTab(textView(kNote + QStringLiteral(
        "\n\nMON*CHA.DAX: $199-byte PC monster records (fields moved, signed bytes\n"
        "widened to words, words and longs byte-swapped).\n"
        "ITEM*.DAX / MON*ITM.DAX: $3F-byte item records.\n"
        "MON*SPC.DAX: 9-byte effect records.")), QStringLiteral("About"));
    return tabs;
}

QWidget *characterFile(const QByteArray &data)
{
    return fieldTable(Kind::Character, data.left(KrynnRecords::characterSize));
}

QWidget *itemFile(const QByteArray &data)
{
    return itemRecords(data, true);
}

QWidget *effectFile(const QByteArray &data)
{
    return effectRecords(data, true);
}

QWidget *itemTypeTable(const QByteArray &data)
{
    auto *tabs = new QTabWidget;
    constexpr int header = 2;
    const int count = (data.size() - header) / KrynnRecords::itemTypeSize;
    auto *table = makeTable({
        QStringLiteral("Type"), QStringLiteral("Name"), QStringLiteral("Slot +0"),
        QStringLiteral("Hands +1"), QStringLiteral("+2 (?)"), QStringLiteral("+3 (?)"),
        QStringLiteral("+4 word (?)"), QStringLiteral("Missile att. +6"),
        QStringLiteral("AC +7"), QStringLiteral("Dmg kind +8"), QStringLiteral("+9 (?)"),
        QStringLiteral("Damage +A/+B"), QStringLiteral("Dmg bonus +C"),
        QStringLiteral("Range +E"), QStringLiteral("Classes +F"),
        QStringLiteral("Flags +10"), QStringLiteral("+11 (?)"), QStringLiteral("Raw")});
    table->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        const QByteArray r = data.mid(header + i * KrynnRecords::itemTypeSize, KrynnRecords::itemTypeSize);
        auto b = [&](int o) { return QString::number(byteAt(r, o)); };
        auto bits = [&](int o) {
            return QStringLiteral("%1").arg(byteAt(r, o), 8, 2, QLatin1Char('0'));
        };
        const int ac = byteAt(r, 6);
        int col = 0;
        table->setItem(i, col++, item(QString::number(i)));
        table->setItem(i, col++, item(KrynnRecords::itemWordName(i)));
        table->setItem(i, col++, item(b(0)));
        table->setItem(i, col++, item(b(1)));
        table->setItem(i, col++, item(b(2)));
        table->setItem(i, col++, item(b(3)));
        table->setItem(i, col++, item(b(4)));
        table->setItem(i, col++, item(b(5)));
        table->setItem(i, col++, item(ac > 0x7F ? QString::number(ac & 0x7F) : QStringLiteral("-")));
        table->setItem(i, col++, item(bits(7)));
        table->setItem(i, col++, item(b(8)));
        table->setItem(i, col++, item(QStringLiteral("%1d%2").arg(byteAt(r, 9)).arg(byteAt(r, 10))));
        table->setItem(i, col++, item(b(11)));
        table->setItem(i, col++, item(b(12)));
        table->setItem(i, col++, item(bits(13)));
        table->setItem(i, col++, item(bits(14)));
        table->setItem(i, col++, item(b(15)));
        table->setItem(i, col++, item(hex(r)));
    }
    table->resizeColumnsToContents();
    tabs->addTab(table, QStringLiteral("Item types"));

    QString text;
    text += QStringLiteral("Amiga Champions of Krynn item type table (\"items\")\n");
    text += QStringLiteral("Size: %1 bytes, header word %2, %3 records of 16 bytes\n\n")
                .arg(data.size()).arg(be16(data, 0)).arg(count);
    text += QStringLiteral(
        "load_items_table (H006) skips the 2-byte header and reads 129 records\n"
        "(the last one past the end of the file) into H005+$336C, $12 bytes per\n"
        "type, indexed by item+$30. File bytes 0-4 go to +0..+4 (word +4 swapped),\n"
        "bytes 5-11 to +6..+$C (word +$C swapped), bytes 12-15 to +$E..+$11.\n\n"
        "Named by the C port: +0 slot/kind, +1 hands used, +6 missile attacks per\n"
        "round, +7 AC (bit 7 = the item gives AC), +8 damage kind bits, +$A/+$B\n"
        "damage dice, +$C damage bonus, +$E range, +$F class mask, +$10 weapon\n"
        "flags ($02 dexterity, $04 strength, $01/$80 ammunition bonuses). Columns marked (?) are\n"
        "not used by the ported code; +2/+3 look like the damage against large\n"
        "creatures but this is not confirmed.\n\n"
        "Names: item name word N (H005+$406E); type N and word N agree for the\n"
        "weapons (Battle Axe 1d8, Dagger 1d4, Two-Handed Sword 1d10...).\n");
    tabs->addTab(textView(text), QStringLiteral("Summary"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

QWidget *savedGame(const QByteArray &data)
{
    // save_game (H066+$3000).
    constexpr int stateA = 1;
    constexpr int stateB = stateA + 0x800;
    constexpr int stateC = stateB + 0x800;
    constexpr int tail = stateC + 0x400;
    auto *tabs = new QTabWidget;

    QString text;
    text += QStringLiteral("Champions of Krynn saved game (save_game, H066+$3000)\n");
    text += QStringLiteral("Size: %1 bytes (expected %2)\n\n").arg(data.size()).arg(tail + 8 + 2 + 12 + 2 + 0x150);
    text += QStringLiteral("$0000       current disk ($246E): %1\n").arg(byteAt(data, 0));
    text += QStringLiteral("$0001-$0800 game state A ($800 bytes)\n");
    text += QStringLiteral("$0801-$1000 game state B ($800 bytes)\n");
    text += QStringLiteral("$1001-$1400 game state C ($400 bytes)\n");
    text += QStringLiteral("$1401       $281C..$2823 (8 bytes): party x on the area map (word) %1, then %2\n")
                .arg(be16(data, tail)).arg(hex(data.mid(tail + 2, 6)));
    text += QStringLiteral("$1409       previous game context ($3DE2): %1\n").arg(byteAt(data, tail + 8));
    text += QStringLiteral("$140A       game mode ($3DE3, 0 menu 1 view 2 camp 5 combat): %1\n").arg(byteAt(data, tail + 9));
    for (int i = 0; i < 3; ++i) {
        const int at = tail + 10 + i * 4;
        const int wall = be16(data, at);
        const int slot = be16(data, at + 2);
        text += QStringLiteral("$%1       wall set %2: WALLDEF id %3, slot %4\n")
                    .arg(at, 4, 16, QLatin1Char('0')).arg(i + 1)
                    .arg(wall == 0xFFFF ? QStringLiteral("empty") : QString::number(wall))
                    .arg(slot == 0xFFFF ? QStringLiteral("-") : QString::number(slot));
    }
    const int countAt = tail + 22;
    const int count = be16(data, countAt);
    text += QStringLiteral("$%1       party size: %2\n").arg(countAt, 4, 16, QLatin1Char('0')).arg(count);
    for (int i = 0; i < 8; ++i) {
        const QByteArray name = data.mid(countAt + 2 + i * 0x2A, 0x2A);
        const QString file = QString::fromLatin1(name).section(QChar(0), 0, 0);
        if (!file.isEmpty())
            text += QStringLiteral("            member %1: %2.sav\n").arg(i + 1).arg(file);
    }
    tabs->addTab(textView(text), QStringLiteral("Summary"));
    tabs->addTab(fieldTable(Kind::StateA, data.mid(stateA, 0x800)), QStringLiteral("State A"));
    tabs->addTab(fieldTable(Kind::StateB, data.mid(stateB, 0x800)), QStringLiteral("State B"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

} // namespace KrynnViews
