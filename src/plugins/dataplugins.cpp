#include "plugins/plugins.h"

#include "core/byteutils.h"
#include "core/daxfile.h"
#include "parse/character.h"
#include "parse/item_names.h"
#include "widgets/characterview.h"
#include "widgets/hexview.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTableWidget>

namespace {

QString itemName(int first, int second, int third)
{
    auto part = [](int index) -> QString {
        if (index >= 0 && index < ItemNames::partsCount)
            return QString::fromLatin1(ItemNames::parts[index]);
        return QStringLiteral("?");
    };
    return QStringLiteral("%1 %2 %3").arg(part(first), part(second), part(third));
}

QString damageDice(int number, int type, int bonus)
{
    if (number <= 0)
        return QStringLiteral("None");
    QString text = QStringLiteral("%1d%2").arg(number).arg(type);
    if (bonus > 0)
        text += QStringLiteral("+%1").arg(bonus);
    return text;
}

QString specialCodeName(int value)
{
    if (value > 1 && value < 128)
        return QStringLiteral("Scroll");
    switch (value) {
    case 129:
        return QStringLiteral("Ring of Wizardry");
    case 131:
        return QStringLiteral("Gauntlets of Ogre Power");
    case 133:
        return QStringLiteral("Girdle of Giant Strength");
    default:
        return QStringLiteral("Magic Effect");
    }
}

QString weaponClassName(int value)
{
    if (value == 1)
        return QStringLiteral("Cutting");
    if (value == 128)
        return QStringLiteral("Blunt");
    return QStringLiteral("None");
}

QString locationName(int value)
{
    static const char *locations[] = {
        "Weapon Hand", "Shield Hand", "Body", "Hands", "Head", "Waist", "Body",
        "Back", "Feet", "Fingers", "Ammo Quiver", "Protection", "Mage", "Cleric"
    };
    if (value >= 0 && value < 14)
        return QString::fromLatin1(locations[value]);
    return QString::number(value);
}

struct ItemRecord
{
    QString name;
    int encumbrance = 0;
    int price = 0;
    int magicBonus = 0;
    int secondaryCode = 0;
    bool ready = false;
    int identified = 0;
    bool cursed = false;
    int bundle = 0;
    int charges = 0;
    int magicalEffect = 0;
    QString specialCode;
    int location = 0;
    int hands = 0;
    QString damageLarge = QStringLiteral("None");
    int rate = 0;
    int protection = 0;
    QString weaponClass;
    QString meleeClass;
    QString damageMedium = QStringLiteral("None");
    int range = 0;
    int classUsage = 0;
    int missileType = 0;
};

struct ItemDetails
{
    int location = 0;
    int hands = 0;
    int largeNumber = 0;
    int largeType = 0;
    int largeBonus = 0;
    int rate = 0;
    int protection = 0;
    QString weaponClass;
    QString meleeClass;
    int mediumNumber = 0;
    int mediumType = 0;
    int mediumBonus = 0;
    int range = 0;
    int classUsage = 0;
    int missileType = 0;
};

ItemDetails readItemDetails(const QByteArray &itemsData, int pointer)
{
    ItemDetails d;
    const int offset = 16 * pointer;
    if (offset < 0 || offset + 16 > itemsData.size())
        return d;

    ByteReader r(itemsData);
    r.seek(offset);
    d.location = r.u8();
    d.hands = r.u8();
    d.largeNumber = r.u8();
    d.largeType = r.u8();
    d.largeBonus = r.u8();
    d.rate = r.u8();
    d.protection = r.u8();
    d.weaponClass = weaponClassName(r.u8());
    d.meleeClass = r.u8() == 0 ? QStringLiteral("Not usable") : QStringLiteral("Usable");
    d.mediumNumber = r.u8();
    d.mediumType = r.u8();
    d.mediumBonus = r.u8();
    d.range = r.u8();
    d.classUsage = r.u8();
    d.missileType = r.u8();
    r.u8();
    return d;
}

QVector<ItemRecord> loadItems(const QString &path)
{
    QVector<ItemRecord> items;
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    const bool isFrua = QFileInfo::exists(QDir(dir).filePath(QStringLiteral("CKIT.CFG")));

    QString itemFileName = info.fileName();
    if (itemFileName.compare(QStringLiteral("ITEMS.DAT"), Qt::CaseInsensitive) == 0)
        itemFileName = QStringLiteral("ITEM.DAT");
    const QString itemPath = QDir(dir).filePath(itemFileName);

    QFile itemFile(itemPath);
    if (!itemFile.open(QIODevice::ReadOnly))
        return items;
    const QByteArray itemData = itemFile.readAll();

    QByteArray detailsData;
    QFile detailsFile(QDir(dir).filePath(QStringLiteral("ITEMS.DAT")));
    if (detailsFile.open(QIODevice::ReadOnly))
        detailsData = detailsFile.readAll();

    ByteReader reader(itemData);
    constexpr int numberOfItems = 254;
    for (int i = 0; i < numberOfItems; ++i) {
        if (reader.remaining() < 16)
            break;

        const int pointer = reader.u8();
        const int third = reader.u8();
        const int second = reader.u8();
        const int first = reader.u8();

        ItemRecord item;
        item.name = itemName(first, second, third);
        item.encumbrance = reader.i16();
        item.price = reader.i16();
        item.magicBonus = reader.u8();
        item.secondaryCode = reader.u8();
        item.ready = reader.u8() == 1;
        item.identified = reader.u8();
        item.cursed = reader.u8() == 1;
        item.bundle = reader.u8();
        item.charges = reader.u8();
        item.magicalEffect = reader.u8();
        item.specialCode = specialCodeName(reader.u8());
        if (isFrua)
            reader.u8();

        const ItemDetails d = readItemDetails(detailsData, pointer);
        item.location = d.location;
        item.hands = d.hands;
        item.damageLarge = damageDice(d.largeNumber, d.largeType, d.largeBonus);
        item.rate = d.rate;
        item.protection = d.protection;
        item.weaponClass = d.weaponClass;
        item.meleeClass = d.meleeClass;
        item.damageMedium = damageDice(d.mediumNumber, d.mediumType, d.mediumBonus);
        item.range = d.range;
        item.classUsage = d.classUsage;
        item.missileType = d.missileType;

        items.append(item);
    }
    return items;
}

void configurePlainText(QPlainTextEdit *view, const QString &text)
{
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(text);
}

} // namespace

namespace {

QWidget *summaryView(const QString &text)
{
    auto *view = new QPlainTextEdit;
    configurePlainText(view, text);
    return view;
}

QString blockText(const QByteArray &data)
{
    int end = data.indexOf('\0');
    if (end < 0)
        end = data.size();
    return QString::fromLatin1(data.left(end)).trimmed();
}

QWidget *amigaItemView(const DaxFile &dax)
{
    auto *tabs = new QTabWidget;

    auto *table = new QTableWidget;
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QStringLiteral("Block"), QStringLiteral("Text"),
                                      QStringLiteral("Bytes")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFont(QFont(QStringLiteral("Courier New"), 9));
    table->setRowCount(dax.blocks().size());
    for (int row = 0; row < dax.blocks().size(); ++row) {
        const DaxFileBlock &block = dax.blocks().at(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(block.id)));
        table->setItem(row, 1, new QTableWidgetItem(blockText(block.data)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(block.data.size())));
    }
    table->resizeColumnsToContents();
    tabs->addTab(table, QStringLiteral("Items"));

    QString summary;
    summary += QStringLiteral("Amiga item name table\n");
    summary += QStringLiteral("Blocks: %1\n").arg(dax.blocks().size());
    summary += QStringLiteral("Each block is decoded with ByteKiller and contains a\n");
    summary += QStringLiteral("null-terminated item name.\n");
    tabs->addTab(summaryView(summary), QStringLiteral("Summary"));
    return tabs;
}

QWidget *amigaCharacterView(const DaxFile &dax)
{
    auto *table = new QTableWidget;
    table->setColumnCount(10);
    table->setHorizontalHeaderLabels({
        QStringLiteral("Block"), QStringLiteral("Name"), QStringLiteral("0x10"),
        QStringLiteral("0x11"), QStringLiteral("0x12"), QStringLiteral("0x13"),
        QStringLiteral("0x14"), QStringLiteral("0x15"), QStringLiteral("Size"),
        QStringLiteral("First bytes")
    });
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFont(QFont(QStringLiteral("Courier New"), 9));
    table->setRowCount(dax.blocks().size());
    for (int row = 0; row < dax.blocks().size(); ++row) {
        const DaxFileBlock &block = dax.blocks().at(row);
        const QByteArray &data = block.data;
        int end = data.indexOf('\0');
        if (end < 0)
            end = qMin(16, data.size());
        table->setItem(row, 0, new QTableWidgetItem(QString::number(block.id)));
        table->setItem(row, 1, new QTableWidgetItem(QString::fromLatin1(data.left(end))));
        for (int i = 0; i < 6; ++i) {
            table->setItem(row, 2 + i, new QTableWidgetItem(
                QString::number(byteAt(data, 0x10 + i))));
        }
        table->setItem(row, 8, new QTableWidgetItem(QString::number(data.size())));
        table->setItem(row, 9, new QTableWidgetItem(QString::fromLatin1(data.left(24).toHex(' '))));
    }
    table->resizeColumnsToContents();
    return table;
}

} // namespace

bool DaxRecordPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    const QString name = info.fileName().toUpper();
    if (!name.endsWith(QStringLiteral(".DAX")))
        return false;

    // Item records (ITEM*.DAX, MON*ITM.DAX) and effect records (MON*SPC.DAX).
    // Character containers (MON*CHA.DAX) use a different Amiga record length
    // and are left to the hex viewer for now.
    return name.contains(QStringLiteral("ITEM"))
           || name.contains(QStringLiteral("ITM"))
           || name.contains(QStringLiteral("SPC"))
           || name.contains(QStringLiteral("CHA"));
}

QWidget *DaxRecordPlugin::createViewer(const PluginParameters &parameters)
{
    const QFileInfo info(parameters.filename);
    const QString name = info.fileName().toUpper();
    DaxFile dax(parameters.filename);
    if (dax.isValid() && dax.isAmiga()) {
        if (name.contains(QStringLiteral("ITEM")) || name.contains(QStringLiteral("ITM")))
            return amigaItemView(dax);
        if (name.contains(QStringLiteral("CHA")))
            return amigaCharacterView(dax);
    }

    CharacterDocument document;
    QString error;
    if (!loadCharacterDocument(parameters.filename, document, &error)) {
        auto *view = new QPlainTextEdit;
        configurePlainText(view, QStringLiteral("Unable to read the DAX record file:\n%1").arg(error));
        return view;
    }
    return new CharacterView(document);
}

bool GameDataPlugin::isSatisfiedBy(const QString &path) const
{
    return QFileInfo(path).fileName().toUpper() == QStringLiteral("GAME001.DAT");
}

QWidget *GameDataPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return new QPlainTextEdit(QStringLiteral("Unable to open the file."));
    ByteReader reader(file.readAll());

    const QString name = reader.fixedString(32);
    const int startingXp = reader.i32();
    const int startingPp = reader.i32();
    const int startingGems = reader.i32();
    const int startingJewelry = reader.i32();
    const int startingModule = reader.u8();
    const int startingTown = reader.u8();
    const int startingEquipment = reader.u8();
    reader.u8();

    QStringList keys;
    for (int i = 0; i < 8; ++i)
        keys.append(reader.fixedString(16));
    QStringList items;
    for (int i = 0; i < 12; ++i)
        items.append(reader.fixedString(16));
    const QString password = reader.fixedString(15);

    QStringList moduleNames;
    for (int i = 0; i < 4; ++i)
        moduleNames.append(QStringLiteral("Overland %1").arg(i));
    for (int i = 0; i < 36; ++i)
        moduleNames.append(QStringLiteral("Dungeon %1").arg(i));

    static const char *equipment[] = {"None", "Poor", "Modest", "Average", "Prosperous",
                                      "+1", "+2", "+3", "+4"};

    QString text;
    text += QStringLiteral("Adventure Design Name\n%1\n\n").arg(name);
    text += QStringLiteral("The Adventure Begins in\n%1 at entry point %2\n\n")
                .arg(moduleNames.value(startingModule), QString::number(startingTown));
    text += QStringLiteral("Each Party Member Starts With\n");
    text += QStringLiteral("%1 Experience Points\n").arg(startingXp);
    text += QStringLiteral("Starting Equipment %1\n")
                .arg(QString::fromLatin1(equipment[qBound(0, startingEquipment, 8)]));
    text += QStringLiteral("Platinum: %1\n").arg(startingPp);
    text += QStringLiteral("Gems: %1\n").arg(startingGems);
    text += QStringLiteral("Jewelry: %1\n\n").arg(startingJewelry);
    text += QStringLiteral("Protection\nPassword: %1\n\n").arg(password);
    text += QStringLiteral("Keys in Special Inventory\n");
    for (int i = 0; i < keys.size(); ++i)
        text += QStringLiteral("Key %1: %2\n").arg(i + 1).arg(keys[i]);
    text += QStringLiteral("\nOther Special Inventory Items\n");
    for (int i = 0; i < items.size(); ++i)
        text += QStringLiteral("Item %1: %2\n").arg(i + 1).arg(items[i]);

    auto *view = new QPlainTextEdit;
    configurePlainText(view, text);
    return view;
}

bool SavedGamePlugin::isSatisfiedBy(const QString &path) const
{
    const QString name = QFileInfo(path).fileName().toUpper();
    return name.startsWith(QStringLiteral("SAVGAM")) && name.endsWith(QStringLiteral(".CSV"));
}

QWidget *SavedGamePlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return new QPlainTextEdit(QStringLiteral("Unable to open the file."));
    ByteReader r(file.readAll());

    r.u8();
    const int noMagic = r.u8();
    r.skip(3);
    const int rounds = r.u8();
    const int turns = r.u8();
    const int hours = r.u8();
    const int days = r.u8();
    const int months = r.u8();
    const int years = r.u8();
    r.skip(5);
    const int facing = r.u8();
    const int gameSpeed = r.u8();
    const int currentModule = r.u8();
    r.skip(2);
    const int initiated = r.u8();
    const int previousColumn = r.u8();
    const int previousRow = r.u8();
    const int searchMode = r.u8();
    const int overland = r.u8();
    r.skip(2);
    const int partyNeverDies = r.u8();
    const int noTreasure = r.u8();
    const int noXp = r.u8();
    const int partySize = r.u8();
    r.u8();
    const int dungeon = r.u8();
    r.u8();
    const int secondOverland = r.u8();
    const int partyColumnOverland = r.u8();
    const int partyRowOverland = r.u8();
    const int combatLevel = r.u8();
    r.skip(4);
    const int noRestZone = r.u8();
    r.skip(3);
    const int allowedTrain = r.u8();
    r.skip(6);
    const int opponentsDistance = r.u8();
    r.skip(2);
    const int friendlyOpponents = r.u8();
    r.u8();
    const int underwater = r.u8();
    r.skip(5);
    const int partyColumnDungeon = r.u8();
    const int partyRowDungeon = r.u8();

    static const char *directions[] = {
        "North", "Northeast", "East", "Southeast", "South", "Southwest", "West", "Northwest"
    };

    QString text;
    text += QStringLiteral("Rounds: %1\n").arg(rounds);
    text += QStringLiteral("Turns: %1\n").arg(turns);
    text += QStringLiteral("Hours: %1\n").arg(hours);
    text += QStringLiteral("Days: %1\n").arg(days);
    text += QStringLiteral("Months: %1\n").arg(months);
    text += QStringLiteral("Years: %1\n").arg(years);
    text += QStringLiteral("Facing Direction: %1\n")
                .arg(QString::fromLatin1(directions[qBound(0, facing, 7)]));
    text += QStringLiteral("Game Speed: %1\n").arg(gameSpeed);
    text += QStringLiteral("Current Module: %1\n").arg(currentModule);
    text += QStringLiteral("Initiated Flag: %1\n").arg(initiated);
    text += QStringLiteral("Previous Column: %1\n").arg(previousColumn);
    text += QStringLiteral("Previous Row: %1\n").arg(previousRow);
    text += QStringLiteral("Search Mode On?: %1\n").arg(searchMode);
    text += QStringLiteral("Overland Flag: %1\n").arg(overland);
    text += QStringLiteral("Party Never Dies Flag: %1\n").arg(partyNeverDies);
    text += QStringLiteral("No Treasure Flag: %1\n").arg(noTreasure);
    text += QStringLiteral("No XP For Combat Flag: %1\n").arg(noXp);
    text += QStringLiteral("Party Size: %1\n").arg(partySize);
    text += QStringLiteral("Dungeon Flag: %1\n").arg(dungeon);
    text += QStringLiteral("Second Overland Flag: %1\n").arg(secondOverland);
    text += QStringLiteral("Party Column (Overland): %1\n").arg(partyColumnOverland);
    text += QStringLiteral("Party Row (Overland): %1\n").arg(partyRowOverland);
    text += QStringLiteral("Combat Level: %1\n").arg(combatLevel);
    text += QStringLiteral("No Rest Zone Flag: %1\n").arg(noRestZone);
    text += QStringLiteral("Allowed Train Flag: %1\n").arg(allowedTrain);
    text += QStringLiteral("Opponents Distance at Start: %1\n").arg(opponentsDistance);
    text += QStringLiteral("Opponents Friendly?: %1\n").arg(friendlyOpponents);
    text += QStringLiteral("Underwater Flag: %1\n").arg(underwater);
    text += QStringLiteral("Party Column (Dungeon): %1\n").arg(partyColumnDungeon);
    text += QStringLiteral("Party Row (Dungeon): %1\n").arg(partyRowDungeon);
    Q_UNUSED(noMagic);

    auto *view = new QPlainTextEdit;
    configurePlainText(view, text);
    return view;
}

bool VaultPlugin::isSatisfiedBy(const QString &path) const
{
    const QString name = QFileInfo(path).fileName().toUpper();
    return name.startsWith(QStringLiteral("VAULT")) && name.endsWith(QStringLiteral(".DAT"));
}

QWidget *VaultPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return new QPlainTextEdit(QStringLiteral("Unable to open the file."));
    ByteReader r(file.readAll());

    const quint32 pp = r.u32();
    const quint32 gems = r.u32();
    const quint32 jewels = r.u32();
    const int itemsInVault = r.i16();
    r.u16();

    QStringList items;
    for (int i = 0; i < 50; ++i) {
        const int pointer = r.u8();
        const int first = r.u8();
        const int second = r.u8();
        const int third = r.u8();
        r.skip(14);
        if (pointer != 255 && itemsInVault > i)
            items.append(itemName(first, second, third));
    }

    QString text;
    text += QStringLiteral("Platinum Pieces: %1\n").arg(pp);
    text += QStringLiteral("Gems: %1\n").arg(gems);
    text += QStringLiteral("Jewels: %1\n").arg(jewels);
    text += QStringLiteral("Items: %1\n")
                .arg(itemsInVault == -1 ? QStringLiteral("None") : QString::number(itemsInVault));
    for (const QString &item : items)
        text += item + QLatin1Char('\n');

    auto *view = new QPlainTextEdit;
    configurePlainText(view, text);
    return view;
}

bool ItemsPlugin::isSatisfiedBy(const QString &path) const
{
    const QString name = QFileInfo(path).fileName().toUpper();
    return name == QStringLiteral("ITEM.DAT") || name == QStringLiteral("ITEMS.DAT");
}

QWidget *ItemsPlugin::createViewer(const PluginParameters &parameters)
{
    const QVector<ItemRecord> items = loadItems(parameters.filename);

    auto *table = new QTableWidget;
    const QStringList headers = {
        QStringLiteral("Name"), QStringLiteral("Encumbrance"), QStringLiteral("Price"),
        QStringLiteral("Magic Bonus"), QStringLiteral("Secondary Code"), QStringLiteral("Ready"),
        QStringLiteral("Identified"), QStringLiteral("Cursed"), QStringLiteral("Items in Bundle"),
        QStringLiteral("Charges"), QStringLiteral("Magical Effect"), QStringLiteral("Special Code"),
        QStringLiteral("Location"), QStringLiteral("Hands"), QStringLiteral("Damage vs. Large"),
        QStringLiteral("Rate"), QStringLiteral("Protection"), QStringLiteral("Weapon Type"),
        QStringLiteral("Melee Usage"), QStringLiteral("Damage vs. Medium/Small"),
        QStringLiteral("Range"), QStringLiteral("Class"), QStringLiteral("Missile Type")
    };
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(items.size());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    table->setFont(QFont(QStringLiteral("Courier New"), 9));

    for (int row = 0; row < items.size(); ++row) {
        const ItemRecord &item = items[row];
        const QStringList values = {
            item.name, QString::number(item.encumbrance), QString::number(item.price),
            QString::number(item.magicBonus), QString::number(item.secondaryCode),
            item.ready ? QStringLiteral("Yes") : QStringLiteral("No"),
            QString::number(item.identified), item.cursed ? QStringLiteral("Yes") : QStringLiteral("No"),
            QString::number(item.bundle), QString::number(item.charges),
            QString::number(item.magicalEffect), item.specialCode, locationName(item.location),
            QString::number(item.hands), item.damageLarge, QString::number(item.rate),
            QString::number(item.protection), item.weaponClass, item.meleeClass,
            item.damageMedium, QString::number(item.range), QString::number(item.classUsage),
            QString::number(item.missileType)
        };
        for (int column = 0; column < values.size(); ++column)
            table->setItem(row, column, new QTableWidgetItem(values[column]));
    }

    table->resizeColumnsToContents();
    return table;
}

bool GlbPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    if (info.suffix().toUpper() != QStringLiteral("GLB"))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 8)
        return false;
    const QByteArray header = file.read(8);
    if (header.left(4) != QByteArrayLiteral("HLIB"))
        return false;
    return readU32(header, 4) == static_cast<quint32>(info.size());
}

QWidget *GlbPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return new QPlainTextEdit(QStringLiteral("Unable to open the file."));
    return new HexView(file.readAll());
}
