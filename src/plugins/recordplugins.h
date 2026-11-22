#pragma once

#include "plugins/plugin.h"

#include <QByteArray>
#include <QString>

// Records of Death Knights of Krynn and Pool of Radiance (Amiga), with the
// field names of their C ports (parse/record_layouts):
//   DKK  CHRDATAn.gam / *.pch character ($E8), .wiz spell block ($DC),
//        .jnk items ($48 each), .eff effects ($0A each), savgam?.pty saved
//        game, globals.daa (packed H004 globals)
//   PoR  *.cha / .sav character ($120), .itm items ($41 each), .spc
//        affects ($0A each)
class GoldBoxRecordPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;

    // Which layout a file uses, or an empty string.
    static QString kindOf(const QString &path, qint64 size);
};

namespace RecordViews {

// Field table of one record of the given layout ("dkk.character", ...).
QWidget *fieldTable(const char *layoutId, const QByteArray &record);

} // namespace RecordViews
