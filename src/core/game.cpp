#include "core/game.h"

#include <QDir>
#include <QFileInfo>

namespace GameDetector {

static bool exists(const QString &dir, const QString &name)
{
    return QFileInfo::exists(QDir(dir).filePath(name));
}

GameList determineGameFrom(const QString &fileName)
{
    const QFileInfo info(fileName);
    const QString dir = info.absolutePath();
    if (dir.isEmpty())
        return GameList::Unknown;

    if (exists(dir, QStringLiteral("POOL.CFG")))
        return GameList::PoolOfRadiance;
    if (exists(dir, QStringLiteral("POOL4.CFG")))
        return GameList::PoolsOfDarkness;
    if (exists(dir, QStringLiteral("BLADES.CFG")))
        return GameList::SecretOfTheSilverBlades;
    if (exists(dir, QStringLiteral("CURSE.CFG")))
        return GameList::CurseOfTheAzureBonds;
    if (exists(dir, QStringLiteral("BUCK.CFG")))
        return GameList::CountdownToDoomsday;
    if (exists(dir, QStringLiteral("MATRIX.CFG")))
        return GameList::MatrixCubed;
    if (exists(dir, QStringLiteral("KRYNN.CFG")))
        return GameList::ChampionsOfKrynn;
    if (exists(dir, QStringLiteral("TREASURE.CFG")))
        return GameList::TreasuresOfTheSavageFrontier;
    if (exists(dir, QStringLiteral("DKK.CFG")))
        return GameList::DeathKnightsOfKrynn;
    if (exists(dir, QStringLiteral("GAME.CFG")) && exists(dir, QStringLiteral("8X8D6.DAX")))
        return GameList::GatewayToTheSavageFrontier;
    if (exists(dir, QStringLiteral("GAME.CFG")) && exists(dir, QStringLiteral("CPIC.DAX")))
        return GameList::NeverwinterNights;

    return GameList::Unknown;
}

} // namespace GameDetector
