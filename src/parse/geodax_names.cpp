#include "parse/geodax_names.h"

QString mapNameFor(GameList game, int id)
{
    switch (game) {
    case GameList::PoolOfRadiance:
        switch (id) {
        case 0: return QStringLiteral("Civilized Area, New Phlan");
        case 1: return QStringLiteral("Buccaneer Base");
        case 2: return QStringLiteral("Cadorna Textile House");
        case 3: return QStringLiteral("Valjevo Castle, North West");
        case 4: return QStringLiteral("Valjevo Castle, North East");
        case 5: return QStringLiteral("Valjevo Castle, South East");
        case 6: return QStringLiteral("Valjevo Castle, South West");
        case 7: return QStringLiteral("Valjevo Castle, Inner Tower");
        case 9: return QStringLiteral("Stojanow Gate");
        case 10: return QStringLiteral("Valhingen Graveyard");
        case 13: return QStringLiteral("Kobold Caves");
        case 14: return QStringLiteral("Kovel Mansion");
        case 15: return QStringLiteral("Mendor's Library");
        case 16: return QStringLiteral("Lizard Men Keep");
        case 17: return QStringLiteral("Nomad Camp");
        case 18: return QStringLiteral("Podal Plaza");
        case 20: return QStringLiteral("Slums");
        case 21: return QStringLiteral("Sokal Keep");
        case 22: return QStringLiteral("Sorcerer's Island, Level 1");
        case 23: return QStringLiteral("Sorcerer's Island, Level 2 and 3");
        case 24: return QStringLiteral("Temple of Bane");
        case 25: return QStringLiteral("Unknown Lair");
        case 26: return QStringLiteral("Unknown Zone");
        case 27: return QStringLiteral("Unknown Lair");
        case 28: return QStringLiteral("Outpost of Zhentil Keep");
        case 29: return QStringLiteral("Kuto's Well");
        case 30: return QStringLiteral("Lizard Men Catacombs");
        case 31: return QStringLiteral("Wealthy Area");
        case 32: return QStringLiteral("Kuto's Well Catacombs");
        default: break;
        }
        break;
    case GameList::CurseOfTheAzureBonds:
        switch (id) {
        case 1: return QStringLiteral("Tilverton City, Thieves' Guild");
        case 3: return QStringLiteral("Tilverton Sewers");
        case 4: return QStringLiteral("The Fire Knift Hideout");
        case 16: return QStringLiteral("Yulash");
        case 17: return QStringLiteral("The Pit of Moander, Levels 1-2");
        case 32: return QStringLiteral("Zhentil Keep, The Shrine of Bane");
        case 33: return QStringLiteral("The Cave of the Beholder");
        case 37: return QStringLiteral("Oxam's Tower, Dungeon, Cavern");
        case 50: return QStringLiteral("Village of Haptooth, Cave of the Dracolich");
        case 51: return QStringLiteral("The Wizard's Tower");
        case 64: return QStringLiteral("The Burial Glen");
        case 66: return QStringLiteral("The Ruins of Myth Drannor");
        case 67: return QStringLiteral("The Grand Ruined Temple, Levels 2-1");
        default: break;
        }
        break;
    case GameList::SecretOfTheSilverBlades:
        switch (id) {
        case 16: return QStringLiteral("New Verdigris");
        case 32: return QStringLiteral("Old Verdigris Ruins");
        case 33: return QStringLiteral("Well of Knowledge");
        case 34: return QStringLiteral("Black Circle Headquarters");
        case 48: return QStringLiteral("The Mines, Level 1");
        case 49: return QStringLiteral("The Mines, Level 2");
        case 50: return QStringLiteral("Temple of Tyr");
        case 80: return QStringLiteral("Crevasses, Level 1");
        case 81: return QStringLiteral("Frost Giant Village");
        case 82: return QStringLiteral("Crevasses, Level 2");
        case 96: return QStringLiteral("Castle Entrance");
        case 97: return QStringLiteral("Middle Level of the Castle");
        case 98: return QStringLiteral("Dreadlord's Sanctum");
        case 64: return QStringLiteral("Dungeon Levels 10-7");
        case 65: return QStringLiteral("Dungeon Levels 4-3");
        case 66: return QStringLiteral("Dungeon Levels 2-1");
        case 68: return QStringLiteral("Drider Base");
        default: break;
        }
        break;
    case GameList::PoolsOfDarkness:
        switch (id) {
        case 1: return QStringLiteral("Playtester Town");
        case 16: return QStringLiteral("Phlan (East)");
        case 17: return QStringLiteral("Phlan (West)");
        case 18: return QStringLiteral("Limbo");
        case 19: return QStringLiteral("Mulmaster, Zhentil Keep");
        case 20: return QStringLiteral("Overland Minis");
        case 21: return QStringLiteral("Hill Giant Steading");
        case 22: return QStringLiteral("Sasha and the Lands of Thar");
        case 32: return QStringLiteral("Fire Giant Cave");
        case 33: return QStringLiteral("Dragons' Aerie");
        case 34: return QStringLiteral("Thorne's Cave");
        case 35: return QStringLiteral("Descent To The Depths");
        case 36: return QStringLiteral("Cave of the Beholder");
        case 37: return QStringLiteral("Vala vs. Vaasa");
        case 39: return QStringLiteral("Dave's Challenge");
        case 40: return QStringLiteral("Manshoon's Tower");
        case 48: return QStringLiteral("Manshoon's Tower");
        case 49: return QStringLiteral("Dragon's Aerie");
        case 50: return QStringLiteral("Temple of Tyr");
        case 52: return QStringLiteral("Moander Minis");
        case 53: return QStringLiteral("Moander's Heart");
        case 64: return QStringLiteral("Dark Phlan");
        case 65: return QStringLiteral("The Palace of Gothmenes");
        case 66: return QStringLiteral("Nacacia and Myth Drannor");
        case 67: return QStringLiteral("Tower of Marcus Levels 9-11");
        case 68: return QStringLiteral("Silk and the Slaves of the Drow");
        case 69: return QStringLiteral("Kalistes' Temple");
        case 70: return QStringLiteral("Tower of Marcus Levels 0-5");
        case 71: return QStringLiteral("Tower of Marcus Levels 6-8");
        case 81: return QStringLiteral("Drow Testing Ground");
        case 82: return QStringLiteral("Kalistes' Parlor, Upper Level");
        case 83: return QStringLiteral("Web Dimension Mines");
        case 84: return QStringLiteral("Kalistes' Parlor, Lower Level");
        default: break;
        }
        break;
    case GameList::GatewayToTheSavageFrontier:
        switch (id) {
        case 1: return QStringLiteral("Yartar");
        case 2: return QStringLiteral("Kraken Complex");
        case 3: return QStringLiteral("Nesme");
        case 4: return QStringLiteral("Silverymoon");
        case 5: return QStringLiteral("Everlund");
        case 6: return QStringLiteral("Neverwinter");
        case 7: return QStringLiteral("Port Llast and Gallant Prince");
        case 8: return QStringLiteral("Luskan");
        case 9: return QStringLiteral("Hosttower of the Arcane");
        case 10: return QStringLiteral("Tuern");
        case 11: return QStringLiteral("Gundarlun");
        case 12: return QStringLiteral("Purple Rocks");
        case 13: return QStringLiteral("Kraken Headquarters");
        case 14: return QStringLiteral("Secomber");
        case 15: return QStringLiteral("Loudwater");
        case 16: return QStringLiteral("Llorkh");
        case 17: return QStringLiteral("The Arena Beneath Llorkh");
        case 18: return QStringLiteral("The Star Mounts");
        case 19: return QStringLiteral("Sundabar");
        case 20: return QStringLiteral("Ascore");
        case 21: return QStringLiteral("Outdoors");
        case 27: return QStringLiteral("Vaalgamon Taunts At Ascore Maze");
        case 28: return QStringLiteral("Victory at Ascore");
        case 99: return QStringLiteral("Playtester Town");
        default: break;
        }
        break;
    case GameList::TreasuresOfTheSavageFrontier:
        switch (id) {
        case 16: return QStringLiteral("Llorkh");
        case 17: return QStringLiteral("Lord Geildarr's Keep");
        case 18: return QStringLiteral("Loudwater");
        case 19: return QStringLiteral("Secomber");
        case 20: return QStringLiteral("Leilon");
        case 21: return QStringLiteral("Smuggler's Dock");
        case 22: return QStringLiteral("Waterdeep and the Caverns");
        case 23: return QStringLiteral("Yartar");
        case 24: return QStringLiteral("Triboar");
        case 25: return QStringLiteral("Longsaddle");
        case 26: return QStringLiteral("Mintarn");
        case 27: return QStringLiteral("Orlumbor");
        case 28: return QStringLiteral("Neverwinter");
        case 29: return QStringLiteral("Port Llast");
        case 30: return QStringLiteral("Mirabar");
        case 31: return QStringLiteral("Luskan");
        case 32: return QStringLiteral("Ruathym");
        case 33: return QStringLiteral("Fireshear");
        case 34: return QStringLiteral("Fireshear Mines");
        case 35: return QStringLiteral("Aurilssbaarg");
        case 36: return QStringLiteral("Bjorn's Hold");
        case 37: return QStringLiteral("Icewolf");
        case 38: return QStringLiteral("Freezefire's Lair");
        case 39: return QStringLiteral("Daggerford");
        case 40: return QStringLiteral("Way Inn");
        case 47: return QStringLiteral("Far Windward");
        case 48: return QStringLiteral("Trackless Sea Tours");
        case 49: return QStringLiteral("Tower of Twilight");
        case 50: return QStringLiteral("The Crossroads");
        case 51: return QStringLiteral("Luskan, Port Llast, Neverwinter Area");
        case 52: return QStringLiteral("Mirabar, Longsaddle Area");
        case 53: return QStringLiteral("Neverinter, Leilon Area");
        case 54: return QStringLiteral("Triboar, Yartar Area");
        case 55: return QStringLiteral("Waterdeep Area");
        case 56: return QStringLiteral("Secomber Area");
        case 57: return QStringLiteral("Loudwater, Llorkh Area");
        case 58: return QStringLiteral("Fireshear");
        case 59: return QStringLiteral("Freezefire's Lair");
        case 60: return QStringLiteral("Orlumbor");
        case 61: return QStringLiteral("Luskan");
        case 62: return QStringLiteral("Luscan");
        default: break;
        }
        break;
    case GameList::NeverwinterNights:
        switch (id) {
        case 1: return QStringLiteral("Neverwinter");
        case 2: return QStringLiteral("Warehouse");
        case 3: return QStringLiteral("Wharves");
        case 4: return QStringLiteral("Southwall");
        case 5: return QStringLiteral("Windy Cliffs");
        case 6: return QStringLiteral("Port Llast");
        case 7: return QStringLiteral("Gallant Prince");
        case 8: return QStringLiteral("Luskan");
        case 9: return QStringLiteral("Vilnask");
        case 10: return QStringLiteral("Crossergate");
        case 11: return QStringLiteral("Lost Hills");
        case 12: return QStringLiteral("Triboar");
        case 13: return QStringLiteral("Chargen");
        case 14: return QStringLiteral("Triboar Lower");
        case 15: return QStringLiteral("Berun's Hill");
        case 16: return QStringLiteral("Longsaddle");
        case 17: return QStringLiteral("Longsaddle Lower");
        case 18: return QStringLiteral("NW Woods");
        case 19: return QStringLiteral("NE Woods");
        case 20: return QStringLiteral("W Woods");
        case 21: return QStringLiteral("E Woods");
        case 22: return QStringLiteral("SW Woods");
        case 23: return QStringLiteral("SE Woods");
        case 24: return QStringLiteral("Floodblest");
        case 25: return QStringLiteral("Nightsedge");
        case 26: return QStringLiteral("Nightsedge Caverns");
        case 27: return QStringLiteral("Sewers");
        case 28: return QStringLiteral("Utheraal");
        case 29: return QStringLiteral("Trisk");
        case 30: return QStringLiteral("Port Llast");
        default: break;
        }
        break;
    case GameList::ChampionsOfKrynn:
        switch (id) {
        case 32: return QStringLiteral("Throtl, Throtl Temple");
        case 34: return QStringLiteral("Throtl Catacombs");
        case 48: return QStringLiteral("Gargath");
        case 49: return QStringLiteral("Gargath Keep");
        case 50: return QStringLiteral("Jalek");
        case 64: return QStringLiteral("Nereka City and Base");
        case 66: return QStringLiteral("Nereka Prison");
        case 67: return QStringLiteral("Tomb of Sir Dargaard");
        case 68: return QStringLiteral("Southern Outpost");
        case 80: return QStringLiteral("Sanction Docks");
        case 81: return QStringLiteral("Temple of Huerzyd");
        case 82: return QStringLiteral("Temple of Duerghast");
        case 96: return QStringLiteral("Citadels");
        case 97: return QStringLiteral("Kernen Square");
        case 99: return QStringLiteral("Ogre Base");
        default: break;
        }
        break;
    case GameList::DeathKnightsOfKrynn:
        switch (id) {
        case 32: return QStringLiteral("Kalaman");
        case 33: return QStringLiteral("Vingaard Keep");
        case 34: return QStringLiteral("Cekos");
        case 36: return QStringLiteral("Gargath Outpost");
        case 49: return QStringLiteral("High Clerist's Tower");
        case 50: return QStringLiteral("Dragon Pit");
        case 51: return QStringLiteral("Throtl Keep");
        case 64: return QStringLiteral("Graveyard");
        case 65: return QStringLiteral("Kuo-Toa Slave Ship");
        case 66: return QStringLiteral("Gnome Village (Quazle)");
        case 67: return QStringLiteral("Turef");
        case 80: return QStringLiteral("Cerberus, Dulcimer");
        case 82: return QStringLiteral("Voice Wood");
        case 83: return QStringLiteral("Shipwreck, Cursed Village, Fun House, Father of Trees");
        case 96: return QStringLiteral("Dargaard Keep, First Floor");
        case 97: return QStringLiteral("Dargaard Keep, Second Floor");
        case 98: return QStringLiteral("Dargaard Keep, Third Floor");
        case 99: return QStringLiteral("Challenge");
        default: break;
        }
        break;
    case GameList::CountdownToDoomsday:
        switch (id) {
        case 16: return QStringLiteral("Chicagorg");
        case 32: return QStringLiteral("Spy Ship");
        case 35: return QStringLiteral("Asteroid Base");
        case 48: return QStringLiteral("Asteroid Base, Level 1");
        case 49: return QStringLiteral("Asteroid Base, Level 2");
        case 50: return QStringLiteral("Pirate Ship, Levels 1-5");
        case 51: return QStringLiteral("Pirate Ship, Levels 6-10");
        case 52: return QStringLiteral("Pirate Ship, Levels 11-15");
        case 65: return QStringLiteral("Desert Runner Village");
        case 66: return QStringLiteral("Mars Base Gradiuvs Mons");
        case 67: return QStringLiteral("More Asteroid Bases");
        case 81: return QStringLiteral("Lowlander Village, Venusian Space Elevator Ruins");
        case 82: return QStringLiteral("Venus RAM Base");
        case 96: return QStringLiteral("Mercury Merchants Area");
        case 97: return QStringLiteral("Mariposa Core");
        case 98: return QStringLiteral("Mercurian Finale, Weapons Control Level");
        case 99: return QStringLiteral("Enemy Ships");
        default: break;
        }
        break;
    case GameList::MatrixCubed:
        switch (id) {
        case 1: return QStringLiteral("Luna Base");
        case 17: return QStringLiteral("Caloris Space Port");
        case 18: return QStringLiteral("Asteroid");
        case 33: return QStringLiteral("Losangelorg Sprawls");
        case 34: return QStringLiteral("Historical Museum, Levels 2-1");
        case 35: return QStringLiteral("Asteroid Base");
        case 38: return QStringLiteral("Losangelorg Sprawls");
        case 49: return QStringLiteral("Lowlander Village");
        case 50: return QStringLiteral("Venus Laboratory, Level 1");
        case 51: return QStringLiteral("Venus Laboratory, Level 2");
        case 52: return QStringLiteral("Lowlander Mines");
        case 64: return QStringLiteral("Luna Base");
        case 65: return QStringLiteral("Tsai Weaponry Labs");
        case 66: return QStringLiteral("RAM Battler, Deimos Level 19-41");
        case 80: return QStringLiteral("Mars Prison, Level 1");
        case 81: return QStringLiteral("PURGE Headquarters, Floors Ground-Upper");
        case 82: return QStringLiteral("NEO Installation");
        case 84: return QStringLiteral("Mars Prison, Level 2");
        case 96: return QStringLiteral("Living Ship");
        case 97: return QStringLiteral("Living Ship");
        case 112: return QStringLiteral("Stormrider University");
        case 113: return QStringLiteral("Genetics Foundation Building, Levels 1-4");
        case 114: return QStringLiteral("Jupiter Finale");
        case 115: return QStringLiteral("Jupiter Aircar");
        default: break;
        }
        break;
    default: break;
    }
    return QString::number(id);
}
