#pragma once

#include <QString>

enum class GameList
{
    PoolOfRadiance,
    CurseOfTheAzureBonds,
    SecretOfTheSilverBlades,
    PoolsOfDarkness,
    ForgottenRealmsUnlimitedAdventures,
    GatewayToTheSavageFrontier,
    TreasuresOfTheSavageFrontier,
    NeverwinterNights,
    ChampionsOfKrynn,
    DeathKnightsOfKrynn,
    DarkQueenOfKyrnn,
    CountdownToDoomsday,
    MatrixCubed,
    Unknown,
};

namespace GameDetector {

GameList determineGameFrom(const QString &fileName);

} // namespace GameDetector
