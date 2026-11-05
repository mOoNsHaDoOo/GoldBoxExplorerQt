#include "parse/character.h"

#include "core/byteutils.h"
#include "core/daxfile.h"
#include "parse/character_tables.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {

QString tableValue(const char *const table[], int count, int index, const QString &fallback = {})
{
    if (index >= 0 && index < count)
        return QString::fromLatin1(table[index]);
    return fallback;
}

Annotation make(int len, int addr, const QString &description,
                const QString &fieldType = QStringLiteral("number"),
                bool duplicated = false, bool firstInRecord = false)
{
    Annotation a;
    a.len = len;
    a.addr = addr;
    a.description = description;
    a.fieldType = fieldType;
    a.duplicated = duplicated;
    a.firstInRecord = firstInRecord;
    return a;
}

QVector<Annotation> poolradAnnotations()
{
    QVector<Annotation> l;
    l << make(15, 0x01, QStringLiteral("character name"), QStringLiteral("string"), false, true)
      << make(1, 0x10, QStringLiteral("strength"))
      << make(1, 0x11, QStringLiteral("intelligence"))
      << make(1, 0x12, QStringLiteral("wisdom"))
      << make(1, 0x13, QStringLiteral("dexterity"))
      << make(1, 0x14, QStringLiteral("constitution"))
      << make(1, 0x15, QStringLiteral("charisma"))
      << make(1, 0x16, QStringLiteral("extraordinary strength"))
      << make(22, 0x17, QStringLiteral("memorized spells"), QStringLiteral("memorized spells"))
      << make(1, 0x2D, QStringLiteral("THAC0"), QStringLiteral("60-number"))
      << make(1, 0x2E, QStringLiteral("race"), QStringLiteral("race"))
      << make(1, 0x2F, QStringLiteral("class"), QStringLiteral("class"))
      << make(2, 0x30, QStringLiteral("age"))
      << make(1, 0x32, QStringLiteral("maximum hp"))
      << make(1, 0x6B, QStringLiteral("highest character level"))
      << make(1, 0x6C, QStringLiteral("creature size"))
      << make(1, 0x6D, QStringLiteral("save vs paralyzation, poison or death"))
      << make(1, 0x6E, QStringLiteral("save vs petrification or polymorph"))
      << make(1, 0x6F, QStringLiteral("save vs rod staff or wand"))
      << make(1, 0x70, QStringLiteral("save vs breath weapon"))
      << make(1, 0x71, QStringLiteral("save vs spell"))
      << make(1, 0x72, QStringLiteral("base movement speed"))
      << make(1, 0x73, QStringLiteral("hit dice"))
      << make(1, 0x74, QStringLiteral("drained levels"))
      << make(1, 0x75, QStringLiteral("drained hp"))
      << make(1, 0x76, QStringLiteral("undead resistance vs turning"))
      << make(1, 0x77, QStringLiteral("thief skills: pick pockets"))
      << make(1, 0x78, QStringLiteral("thief skills: open locks"))
      << make(1, 0x79, QStringLiteral("thief skills: find/remove traps"))
      << make(1, 0x7A, QStringLiteral("thief skills: move silently"))
      << make(1, 0x7B, QStringLiteral("thief skills: hide in shadows"))
      << make(1, 0x7C, QStringLiteral("thief skills: hear noise"))
      << make(1, 0x7D, QStringLiteral("thief skills: climb walls"))
      << make(1, 0x7E, QStringLiteral("thief skills: read languages"))
      << make(4, 0x7F, QStringLiteral("effects ptr"))
      << make(1, 0x83, QStringLiteral("unknown byte 1"))
      << make(1, 0x84, QStringLiteral("NPC Status"))
      << make(1, 0x85, QStringLiteral("NPC treasure share"))
      << make(1, 0x86, QStringLiteral("unknown byte 2"))
      << make(1, 0x87, QStringLiteral("unknown byte 3"))
      << make(1, 0x88, QStringLiteral("unknown byte 4"))
      << make(2, 0x89, QStringLiteral("copper"))
      << make(2, 0x8B, QStringLiteral("silver"))
      << make(2, 0x8D, QStringLiteral("electrum"))
      << make(2, 0x8E, QStringLiteral("gold"))
      << make(2, 0x90, QStringLiteral("platinum"))
      << make(2, 0x92, QStringLiteral("gems"))
      << make(2, 0x94, QStringLiteral("jewels"))
      << make(1, 0x96, QStringLiteral("cleric levels"))
      << make(1, 0x97, QStringLiteral("druid levels"))
      << make(1, 0x98, QStringLiteral("fighter levels"))
      << make(1, 0x99, QStringLiteral("paladin levels"))
      << make(1, 0x9A, QStringLiteral("ranger levels"))
      << make(1, 0x9B, QStringLiteral("magic user levels"))
      << make(1, 0x9C, QStringLiteral("thief levels"))
      << make(1, 0x9D, QStringLiteral("monk levels"))
      << make(1, 0x9E, QStringLiteral("gender"))
      << make(1, 0x9F, QStringLiteral("monster type"))
      << make(1, 0xA0, QStringLiteral("alignment"), QStringLiteral("alignment"))
      << make(1, 0xA1, QStringLiteral("number of primary attacks *2"))
      << make(1, 0xA2, QStringLiteral("number of secondary attacks *2"))
      << make(1, 0xA3, QStringLiteral("primary attack damage dice number"))
      << make(1, 0xA4, QStringLiteral("secondary attack damage dice number"))
      << make(1, 0xA5, QStringLiteral("primary attack damage dice sides"))
      << make(1, 0xA6, QStringLiteral("secondary attack damage dice sides"))
      << make(1, 0xA7, QStringLiteral("primary attack damage dice modifier"))
      << make(1, 0xA8, QStringLiteral("secondary attack damage dice modifier"))
      << make(1, 0xA9, QStringLiteral("armor class"), QStringLiteral("60-number"))
      << make(1, 0xAA, QStringLiteral("strength bonus allowed"))
      << make(1, 0xAB, QStringLiteral("combat icon"))
      << make(4, 0xAC, QStringLiteral("experience points"))
      << make(1, 0xB0, QStringLiteral("class item usage flags"))
      << make(1, 0xB1, QStringLiteral("hit points rolled"))
      << make(1, 0xB2, QStringLiteral("cleric 1st level spell slots"))
      << make(1, 0xB3, QStringLiteral("cleric 2nd level spell slots"))
      << make(1, 0xB4, QStringLiteral("cleric 3rd level spell slots"))
      << make(1, 0xB5, QStringLiteral("magic user 1st level spell slots"))
      << make(1, 0xB6, QStringLiteral("magic user 2nd level spell slots"))
      << make(1, 0xB7, QStringLiteral("magic user 3rd level spell slots"))
      << make(2, 0xB8, QStringLiteral("base XP for defeating this monster"))
      << make(1, 0xBA, QStringLiteral("bonus XP per monster HP for defeating"))
      << make(1, 0xBB, QStringLiteral("head portrait"))
      << make(1, 0xBC, QStringLiteral("body portrait"))
      << make(1, 0xBD, QStringLiteral("head icon"))
      << make(1, 0xBE, QStringLiteral("weapon icon"))
      << make(1, 0xBF, QStringLiteral("unknown byte 5"))
      << make(1, 0xC0, QStringLiteral("icon size"))
      << make(1, 0xC1, QStringLiteral("body color"))
      << make(1, 0xC2, QStringLiteral("arm color"))
      << make(1, 0xC3, QStringLiteral("leg color"))
      << make(1, 0xC4, QStringLiteral("head color"))
      << make(1, 0xC5, QStringLiteral("shield color"))
      << make(1, 0xC6, QStringLiteral("weapon color"))
      << make(1, 0xC7, QStringLiteral("special vulnerabilty flags"))
      << make(56, 0xC8, QStringLiteral("items"))
      << make(1, 0x100, QStringLiteral("hands used"))
      << make(1, 0x101, QStringLiteral("unknown byte 6"))
      << make(2, 0x102, QStringLiteral("encumbrance"))
      << make(1, 0x104, QStringLiteral("unknown byte 7"))
      << make(1, 0x105, QStringLiteral("unknown byte 8"))
      << make(1, 0x106, QStringLiteral("unknown byte 9"))
      << make(4, 0x107, QStringLiteral("actions"))
      << make(1, 0x10B, QStringLiteral("unknown byte 10"))
      << make(1, 0x10C, QStringLiteral("health status"))
      << make(1, 0x10D, QStringLiteral("in combat"))
      << make(1, 0x10E, QStringLiteral("side in combat"))
      << make(1, 0x10F, QStringLiteral("quick fight flag"))
      << make(1, 0x110, QStringLiteral("current THAC0"), QStringLiteral("60-number"))
      << make(1, 0x111, QStringLiteral("current AC"), QStringLiteral("60-number"))
      << make(1, 0x112, QStringLiteral("AC for rear attacks"), QStringLiteral("60-number"))
      << make(1, 0x113, QStringLiteral("primary attacks left"))
      << make(1, 0x114, QStringLiteral("secondary attacks left"))
      << make(1, 0x115, QStringLiteral("current primary attack damage dice number"))
      << make(1, 0x116, QStringLiteral("current secondary attack damage dice number"))
      << make(1, 0x117, QStringLiteral("current primary attack damage dice sides"))
      << make(1, 0x118, QStringLiteral("current secondary attack dice sides"))
      << make(1, 0x119, QStringLiteral("current primary attack bonus"))
      << make(1, 0x11A, QStringLiteral("current secondary attack bonus"))
      << make(1, 0x11B, QStringLiteral("current HP"))
      << make(1, 0x11C, QStringLiteral("current movement"));
    return l;
}

QVector<Annotation> itemAnnotations()
{
    QVector<Annotation> l;
    l << make(45, 0x01, QStringLiteral("item name"), QStringLiteral("string"), false, true)
      << make(1, 0x2E, QStringLiteral("item type"), QStringLiteral("item type"))
      << make(1, 0x2F, QStringLiteral("name component 1"), QStringLiteral("name component"))
      << make(1, 0x30, QStringLiteral("name component 2"), QStringLiteral("name component"))
      << make(1, 0x31, QStringLiteral("name component 3"), QStringLiteral("name component"))
      << make(1, 0x32, QStringLiteral("item bonus"))
      << make(1, 0x33, QStringLiteral("save bonus"))
      << make(1, 0x34, QStringLiteral("item is readied"))
      << make(1, 0x35, QStringLiteral("name components revealed"), QStringLiteral("name concealed"))
      << make(1, 0x36, QStringLiteral("cursed"))
      << make(2, 0x37, QStringLiteral("weight"))
      << make(1, 0x39, QStringLiteral("stack size"))
      << make(2, 0x3A, QStringLiteral("item value"))
      << make(1, 0x3C, QStringLiteral("effect 1"), QStringLiteral("special effect"))
      << make(1, 0x3D, QStringLiteral("effect 2"), QStringLiteral("special effect"))
      << make(1, 0x3E, QStringLiteral("effect 3"), QStringLiteral("special effect"));
    for (Annotation &a : l)
        a.isItem = true;
    return l;
}

QVector<Annotation> itemTemplateAnnotations()
{
    QVector<Annotation> l;
    l << make(1, 0x00, QStringLiteral("location where worn"))
      << make(1, 0x01, QStringLiteral("hands required to wield"))
      << make(1, 0x02, QStringLiteral("number of damage dice vs Large sized enemies"))
      << make(1, 0x03, QStringLiteral("sides of damage dice vs Large sized enemies"))
      << make(1, 0x04, QStringLiteral("dice damage bonus vs Large sized enemies"))
      << make(1, 0x05, QStringLiteral("rate of fire"))
      << make(1, 0x06, QStringLiteral("protective value"))
      << make(1, 0x07, QStringLiteral("damage type"))
      << make(1, 0x08, QStringLiteral("melee weapon flag"))
      << make(1, 0x09, QStringLiteral("number of damage dice vs Small/Medium sized enemies"))
      << make(1, 0x0A, QStringLiteral("sides of damage dice vs Small/Medium sized enemies"))
      << make(1, 0x0B, QStringLiteral("dice damage bonus vs Small/Medium sized enemies"))
      << make(1, 0x0C, QStringLiteral("range"))
      << make(1, 0x0D, QStringLiteral("class restrictions"))
      << make(1, 0x0E, QStringLiteral("ammo type used"))
      << make(1, 0x0F, QStringLiteral("unknown byte"));
    return l;
}

QVector<Annotation> effectAnnotations()
{
    QVector<Annotation> l;
    l << make(1, 0x00, QStringLiteral("effect code"), QStringLiteral("special effect"))
      << make(2, 0x01, QStringLiteral("effect duration (0=permament)"))
      << make(1, 0x03, QStringLiteral("effect data"))
      << make(1, 0x04, QStringLiteral("effect table flag"))
      << make(4, 0x05, QStringLiteral("next effect ptr"));
    return l;
}

} // namespace

QString Annotation::interpret(const QByteArray &data) const
{
    if (addr > data.size())
        return QStringLiteral("file format error");

    QString result;

    if (fieldType == QStringLiteral("string")) {
        const int nameLen = byteAt(data, addr - 1);
        result += QString::fromLatin1(data.mid(addr, nameLen));
    }

    if (fieldType == QStringLiteral("name concealed")) {
        const quint8 value = byteAt(data, addr);
        if (value == 0)
            result += QStringLiteral("name fully revealed");
        if ((value & 1) == 1)
            result += QStringLiteral("component 3 hidden. ");
        if ((value & 2) == 2)
            result += QStringLiteral("component 2 hidden. ");
        if ((value & 4) == 4)
            result += QStringLiteral("component 1 hidden. ");
    }

    if (fieldType == QStringLiteral("special effect")) {
        constexpr int clericalScroll = 61;
        constexpr int muScroll = 62;
        if (isItem) {
            constexpr int itemTypeAddr = 0x2E;
            const int itemType = byteAt(data, itemTypeAddr);
            if (itemType == clericalScroll || itemType == muScroll) {
                const int spell = byteAt(data, addr);
                if (spell > 0) {
                    if (spell > 56)
                        result += QStringLiteral("[%1]: unknown spell").arg(spell);
                    else
                        result += QStringLiteral("[%1]: %2").arg(spell).arg(
                            tableValue(CharacterTables::spells, CharacterTables::spellsCount, spell - 1));
                    return result;
                }
            }
        }

        const int value = byteAt(data, addr);
        if (value > 0) {
            if (description == QStringLiteral("effect 3") && value > 127) {
                result += QStringLiteral("[%1]: Activated by readying").arg(value);
                return result;
            }
            if (description == QStringLiteral("effect 1") && byteAt(data, 0x3E) == 0) {
                result += QStringLiteral("Charges: %1").arg(value);
            } else {
                result += QStringLiteral("[%1]: %2")
                              .arg(value)
                              .arg(QString::fromLatin1(CharacterTables::specialAbility(value)));
            }
        } else {
            result += QStringLiteral("0");
        }
    }

    if (fieldType == QStringLiteral("name component")) {
        const int index = byteAt(data, addr);
        result += QStringLiteral("[%1]: %2").arg(index).arg(
            tableValue(CharacterTables::itemTable, CharacterTables::itemTableCount, index));
    }

    if (fieldType == QStringLiteral("item type")) {
        const int index = byteAt(data, addr);
        result += QStringLiteral("[%1]: %2").arg(index).arg(
            tableValue(CharacterTables::itemTypes, CharacterTables::itemTypesCount, index));
    }

    if (fieldType == QStringLiteral("number")) {
        if (len == 1) {
            result += QString::number(byteAt(data, addr));
        } else if (len == 2) {
            result += QString::number(byteAt(data, addr) + (byteAt(data, addr + 1) << 8));
        }
    }

    if (fieldType == QStringLiteral("60-number"))
        result += QString::number(60 - byteAt(data, addr));

    if (fieldType == QStringLiteral("class")) {
        result += tableValue(CharacterTables::classes, CharacterTables::classesCount,
                             byteAt(data, addr));
    }

    if (fieldType == QStringLiteral("race")) {
        result += tableValue(CharacterTables::races, CharacterTables::racesCount,
                             byteAt(data, addr));
    }

    if (fieldType == QStringLiteral("alignment")) {
        result += tableValue(CharacterTables::alignments, CharacterTables::alignmentsCount,
                             byteAt(data, addr));
    }

    if (fieldType == QStringLiteral("memorized spells")) {
        for (int i = 0; i < len; ++i) {
            const int spell = byteAt(data, addr + i);
            if (spell > 0) {
                result += tableValue(CharacterTables::spells, CharacterTables::spellsCount,
                                     spell - 1);
                result += QStringLiteral(", ");
            }
        }
    }

    return result;
}

int characterRecordLength(CharacterFormat format)
{
    switch (format) {
    case CharacterFormat::Poolrad:
        return 0x11D;
    case CharacterFormat::Item:
    case CharacterFormat::DaxItem:
        return 63;
    case CharacterFormat::ItemTemplate:
        return 16;
    case CharacterFormat::Spc:
    case CharacterFormat::DaxSpc:
        return 9;
    }
    return 0;
}

QVector<Annotation> characterAnnotations(CharacterFormat format)
{
    switch (format) {
    case CharacterFormat::Poolrad:
        return poolradAnnotations();
    case CharacterFormat::Item:
    case CharacterFormat::DaxItem:
        return itemAnnotations();
    case CharacterFormat::ItemTemplate:
        return itemTemplateAnnotations();
    case CharacterFormat::Spc:
    case CharacterFormat::DaxSpc:
        return effectAnnotations();
    }
    return {};
}

QString characterFormatName(CharacterFormat format)
{
    switch (format) {
    case CharacterFormat::Poolrad:
        return QStringLiteral("poolrad");
    case CharacterFormat::Item:
        return QStringLiteral("item");
    case CharacterFormat::DaxItem:
        return QStringLiteral("daxitem");
    case CharacterFormat::ItemTemplate:
        return QStringLiteral("item template");
    case CharacterFormat::Spc:
        return QStringLiteral("spc");
    case CharacterFormat::DaxSpc:
        return QStringLiteral("daxspc");
    }
    return QStringLiteral("poolrad");
}

bool loadCharacterDocument(const QString &path, CharacterDocument &document, QString *error)
{
    document = {};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QFileInfo info(path);
    const QString base = info.fileName().toUpper();
    const QString ext = info.suffix().toUpper();

    auto addSplitRecords = [&](const QByteArray &data, int start, int recordLength,
                               const QString &prefix) {
        if (recordLength <= 0) {
            document.records.append({prefix, data, 0});
            return;
        }
        const int count = qMax(0, (data.size() - start) / recordLength);
        if (count <= 1) {
            document.records.append({prefix, data, 0});
            return;
        }
        for (int i = 0; i < count; ++i) {
            document.records.append({QStringLiteral("%1-%2").arg(prefix).arg(i),
                                     data.mid(start + i * recordLength, recordLength), i});
        }
    };

    if (ext == QStringLiteral("DAX")) {
        DaxFile dax(path);
        if (!dax.isValid()) {
            if (error)
                *error = dax.errorString();
            return false;
        }

        document.format = CharacterFormat::Poolrad;
        if (base.contains(QStringLiteral("SPC")))
            document.format = CharacterFormat::DaxSpc;
        else if (base.contains(QStringLiteral("ITM")) || base.contains(QStringLiteral("ITEM")))
            document.format = CharacterFormat::DaxItem;

        const int recordLength = characterRecordLength(document.format);
        for (const DaxFileBlock &block : dax.blocks())
            addSplitRecords(block.data, 0, recordLength, QString::number(block.id));
        return true;
    }

    if (base.contains(QStringLiteral("ITEMS"))) {
        document.format = CharacterFormat::ItemTemplate;
        document.displayItemTypeName = true;
        addSplitRecords(bytes, 2, 16, characterFormatName(document.format));
        return true;
    }
    if (ext == QStringLiteral("ITM")) {
        document.format = CharacterFormat::Item;
        addSplitRecords(bytes, 0, 63, characterFormatName(document.format));
        return true;
    }
    if (ext == QStringLiteral("SPC")) {
        document.format = CharacterFormat::Spc;
        addSplitRecords(bytes, 0, 9, characterFormatName(document.format));
        return true;
    }

    document.format = CharacterFormat::Poolrad;
    document.records.append({QStringLiteral("Character"), bytes, 0});
    return true;
}
