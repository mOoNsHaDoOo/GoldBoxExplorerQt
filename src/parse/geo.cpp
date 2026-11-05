#include "parse/geo.h"

#include "core/byteutils.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

GeoFile::GeoFile(const QString &path)
    : m_path(path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return;
    }
    m_bytes = file.readAll();
    parse();
}

void GeoFile::parse()
{
    ByteReader reader(m_bytes);

    reader.skip(26);
    m_height = reader.u8();
    m_width = reader.u8();
    for (int i = 0; i < 3; ++i)
        m_wallSlots.append(reader.u8());
    m_areaViewFlag = reader.u8();
    for (int i = 0; i < 4; ++i)
        m_backdropSlots.append(reader.u8());
    m_dungeonCombatArt = reader.u8();
    m_wildernessCombatArt = reader.u8();

    for (int i = 0; i < 8; ++i) {
        EntryPoint p;
        p.row = reader.u8();
        p.column = reader.u8();
        p.direction = reader.u8();
        reader.u8();
        m_entryPoints.append(p);
    }

    for (int i = 0; i < 8; ++i) {
        RestEvent e;
        e.minutes = reader.u8();
        reader.u8();
        e.eventIndex = reader.u8();
        e.chance = reader.u8();
        m_restEvents.append(e);
    }

    for (int i = 0; i < 8; ++i) {
        StepEvent e;
        e.steps = reader.u8();
        reader.u8();
        e.eventIndex = reader.u8();
        e.zoneFlags = reader.u8();
        m_stepEvents.append(e);
    }

    reader.skip(8);
    m_moduleName = reader.fixedString(16).trimmed();

    for (int i = 0; i < 8; ++i) {
        QString zone;
        for (int j = 0; j < 16; ++j) {
            const int b = reader.u8();
            if (b != 0)
                zone.append(QChar(b));
        }
        m_zones[i] = zone;
    }

    m_summonedMonster = reader.u8();
    m_undeadTurnDifficulty = reader.u8();
    for (int i = 0; i < 8; ++i)
        m_overlandWildernessSlots.append(reader.u8());
    reader.skip(16);
    m_campPicture = reader.u8();
    m_treasurePicture = reader.u8();
    reader.skip(8);

    for (int i = 0; i < 576; ++i) {
        Grid grid;
        grid.north = reader.u8();
        grid.east = reader.u8();
        grid.south = reader.u8();
        grid.west = reader.u8();
        grid.event = reader.u8();
        grid.backdrop = reader.u8();
        m_map.append(grid);
    }

    reader.skip(8);
    for (int i = 0; i < 100; ++i) {
        EventRecord e;
        e.type = reader.u8();
        e.doFlag = reader.u8();
        e.condition = reader.u8();
        e.chain = reader.u8();
        reader.skip(16);
        m_events.append(e);
    }
    reader.skip(4);
    reader.u8();
    reader.skip(400);
    reader.skip(6760);

    m_valid = reader.ok() || m_bytes.size() >= 20;
}

QString GeoFile::eventName(int index) const
{
    static const char *names[] = {
        "No Event", "Combat Event", "Text Statement", "Give Treasure", "Damage",
        "Stairs", "Training Hall", "Tavern", "Shop", "Temple", "Question-Button",
        "Transfer Module", "Guided Tour", "Add NPC", "NPC Says", "Encounter",
        "Utilities", "Sounds", "Who Trys", "Who Pays", "Enter Password",
        "Question-List", "Small Town", "Chain", "Vault", "Combat Treasure",
        "Gain Experience", "Pass Time", "Event", "Camp", "Event", "Event",
        "Remove NPC", "Pick One Combat", "Teleporter", "Quest Stage",
        "Question-Yes/No", "Tavern Tales", "Special Item"
    };
    if (index >= 0 && index < static_cast<int>(sizeof(names) / sizeof(names[0])))
        return QString::fromLatin1(names[index]);
    return QStringLiteral("Event");
}

QString GeoFile::summary() const
{
    return buildSummary();
}

QString GeoFile::buildSummary() const
{
    QString s;
    const bool overland = m_wallSlots.size() >= 3 && m_wallSlots[0] == 255
                          && m_wallSlots[1] == 255 && m_wallSlots[2] == 255;

    if (overland) {
        s += QStringLiteral("Overland Global Information\n\n");
        s += QStringLiteral("Adventure Module Name:\n%1\n\n").arg(m_moduleName);
        s += QStringLiteral("Height: %1 Width: %2\n\n").arg(m_height).arg(m_width);
        s += QStringLiteral("Entry Points\n");
        for (int i = 0; i < m_entryPoints.size(); ++i)
            s += QStringLiteral("%1 (%2,%3)\n").arg(i + 1).arg(m_entryPoints[i].column).arg(m_entryPoints[i].row);
        s += QStringLiteral("\nSummoned Monster: %1\n\n").arg(m_summonedMonster);
        for (int i = 0; i < 8; ++i)
            s += QStringLiteral("Zone Name %1: %2\n").arg(i + 1).arg(m_zones[i]);
        s += QStringLiteral("\nMap Art\n%1\n").arg(m_backdropSlots.value(0));
        s += QStringLiteral("\nOutdoor Combat Art for Each Zone\n");
        for (int i = 0; i < 8; ++i)
            s += QStringLiteral("Zone %1: %2\n").arg(i + 1).arg(m_overlandWildernessSlots.value(i));
        s += QStringLiteral("\nStep Events\n");
        for (int i = 0; i < m_stepEvents.size(); ++i) {
            s += QStringLiteral("%1) A %2 Event\nHappens on Step %3 in Zones:\n\n")
                     .arg(i + 1).arg(eventName(m_events.value(m_stepEvents[i].eventIndex).type))
                     .arg(m_stepEvents[i].steps);
        }
        s += QStringLiteral("More Module Options\n\n");
        s += QStringLiteral("Undead are more difficult to turn by %1\n\n").arg(m_undeadTurnDifficulty);
        s += QStringLiteral("Camp Picture: %1\n\n").arg(m_campPicture);
        s += QStringLiteral("Treasure Picture: %1\n\n").arg(m_treasurePicture);
    } else {
        s += QStringLiteral("Dungeon Global Information\n\n");
        s += QStringLiteral("Adventure Module Name:\n%1\n\n").arg(m_moduleName);
        s += QStringLiteral("Height: %1 Width: %2\n\n").arg(m_height).arg(m_width);
        s += QStringLiteral("Allow Area View? %1\n\n").arg(m_areaViewFlag == 0 ? QStringLiteral("Yes") : QStringLiteral("No"));
        s += QStringLiteral("Summoned Monster: %1\n\n").arg(m_summonedMonster);
        for (int i = 0; i < 8; ++i)
            s += QStringLiteral("Zone Name %1: %2\n").arg(i + 1).arg(m_zones[i]);
        s += QStringLiteral("\nWall Art Sets\n\n");
        for (int i = 0; i < m_wallSlots.size(); ++i)
            s += QStringLiteral("Wall Art Slot %1: %2\n").arg(i + 1).arg(m_wallSlots[i]);
        s += QStringLiteral("\nFloor and Ceiling Art\n\n");
        for (int i = 0; i < m_backdropSlots.size(); ++i)
            s += QStringLiteral("Floor Art Slot %1: %2\n").arg(i + 1).arg(m_backdropSlots[i]);
        s += QStringLiteral("\nCombat Art\n\n");
        s += QStringLiteral("Indoor Combat Art: %1\n").arg(m_dungeonCombatArt);
        s += QStringLiteral("Outdoor Combat Art: %1\n\n").arg(m_wildernessCombatArt);
        s += QStringLiteral("Step Events\n\n");
        for (int i = 0; i < m_stepEvents.size(); ++i) {
            s += QStringLiteral("%1) A %2 Event\nHappens on Step %3 in Zones:\n\n")
                     .arg(i + 1).arg(eventName(m_events.value(m_stepEvents[i].eventIndex).type))
                     .arg(m_stepEvents[i].steps);
        }
        s += QStringLiteral("More Module Options\n\n");
        s += QStringLiteral("Undead are more difficult to turn by %1\n\n").arg(m_undeadTurnDifficulty);
        s += QStringLiteral("Camp Picture: %1\n\n").arg(m_campPicture);
        s += QStringLiteral("Treasure Picture: %1\n\n").arg(m_treasurePicture);
    }

    s += QStringLiteral("Rest Events\n\n");
    for (int i = 0; i < m_restEvents.size(); ++i) {
        const RestEvent &e = m_restEvents[i];
        s += QStringLiteral("In Zone %1 a %2 Event\n").arg(i + 1).arg(eventName(m_events.value(e.eventIndex).type));
        s += QStringLiteral("Has a %1 % Chance of Occurring\n").arg(e.chance);
        s += QStringLiteral("Every %1 Minutes.\n\n").arg(e.minutes);
    }
    return s;
}

QString GeoFile::map() const
{
    QString s;
    for (const Grid &grid : m_map) {
        s += QStringLiteral("N:%1 E:%2 S:%3 W:%4 EVT:%5 BACK:%6\n")
                 .arg(grid.north).arg(grid.east).arg(grid.south).arg(grid.west)
                 .arg(grid.event).arg(grid.backdrop);
    }
    return s;
}
