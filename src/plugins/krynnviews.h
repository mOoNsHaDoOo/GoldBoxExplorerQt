#pragma once

#include "parse/krynn_fields.h"

#include <QByteArray>
#include <QString>

class DaxFile;
class QWidget;

// Widgets for the Champions of Krynn (Amiga) record layouts documented by the
// readable C port: character/monster, item and effect records, the "items"
// type table and savgam*.dat.
namespace KrynnViews {

QWidget *fieldTable(KrynnFields::Kind kind, const QByteArray &memory);

// ITEM*.DAX, MON*ITM.DAX, MON*CHA.DAX, MON*SPC.DAX (little-endian DAX).
QWidget *daxRecords(const DaxFile &dax, const QString &fileName);

// .sav (one $1AA record), .swg ($48 items), .fx ($0A effects) as stored.
QWidget *characterFile(const QByteArray &data);
QWidget *itemFile(const QByteArray &data);
QWidget *effectFile(const QByteArray &data);

// "items": 2-byte header + 16-byte item type records (load_items_table).
QWidget *itemTypeTable(const QByteArray &data);

// savgam<A-J>.dat (save_game, H066+$3000).
QWidget *savedGame(const QByteArray &data);

} // namespace KrynnViews
