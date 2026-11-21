#pragma once

#include "parse/krynn_fields.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// Record layouts of the Amiga Champions of Krynn port, as loaded by the
// readable C port (H066 Load_MonsterRecord, H006 load_items_table, save
// files in H066). The DAX files keep the PC little-endian records; these
// helpers convert them to the in-memory (big-endian) layout that the
// .sav/.swg/.fx files store verbatim, so one field table labels them all.
namespace KrynnRecords {

constexpr int characterSize = 0x1AA; // CHRDATA*.sav, monster records in memory
constexpr int itemSize = 0x48;       // CHRDATA*.swg entries
constexpr int effectSize = 0x0A;     // CHRDATA*.fx entries
constexpr int pcMonsterSize = 0x199; // MON*CHA.DAX block (without the $100 margin)
constexpr int pcItemSize = 0x3F;     // ITEM*.DAX / MON*ITM.DAX records
constexpr int pcEffectSize = 9;      // MON*SPC.DAX records
constexpr int itemTypeSize = 16;     // "items" file records

QByteArray monsterToMemory(const QByteArray &pcRecord);
QByteArray itemToMemory(const QByteArray &pcRecord);
QByteArray effectToMemory(const QByteArray &pcRecord);

struct FieldValue
{
    int offset = 0;
    int size = 0;
    QString name;
    QString value;
    QString raw;
};

QVector<FieldValue> describe(KrynnFields::Kind kind, const QByteArray &memory);

// "Long Sword +1" style name from the three item name words (+$32..+$34).
QString itemWordName(int word);
QString itemName(const QByteArray &itemMemory);

// Folder heuristic: the Amiga Krynn data disks hold .daa files and the
// extension-less "items" table next to the .dax files.
bool looksLikeAmigaKrynnFolder(const QString &filePath);

} // namespace KrynnRecords
