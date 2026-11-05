#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

class GeoFile
{
public:
    explicit GeoFile(const QString &path);

    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    QString summary() const;
    QString map() const;
    QByteArray rawBytes() const { return m_bytes; }

private:
    struct Grid
    {
        int north = 0;
        int east = 0;
        int south = 0;
        int west = 0;
        int event = 0;
        int backdrop = 0;
    };
    struct EventRecord
    {
        int type = 0;
        int doFlag = 0;
        int condition = 0;
        int chain = 0;
    };
    struct RestEvent
    {
        int minutes = 0;
        int eventIndex = 0;
        int chance = 0;
    };
    struct StepEvent
    {
        int steps = 0;
        int eventIndex = 0;
        int zoneFlags = 0;
    };
    struct EntryPoint
    {
        int row = 0;
        int column = 0;
        int direction = 0;
    };

    void parse();
    QString eventName(int index) const;
    QString buildSummary() const;

    QString m_path;
    QByteArray m_bytes;
    QString m_error;
    bool m_valid = false;

    int m_height = 0;
    int m_width = 0;
    QVector<int> m_wallSlots;
    int m_areaViewFlag = 0;
    QVector<int> m_backdropSlots;
    int m_dungeonCombatArt = 0;
    int m_wildernessCombatArt = 0;
    QVector<EntryPoint> m_entryPoints;
    QVector<RestEvent> m_restEvents;
    QVector<StepEvent> m_stepEvents;
    QString m_moduleName;
    QString m_zones[8];
    int m_summonedMonster = 0;
    int m_undeadTurnDifficulty = 0;
    QVector<int> m_overlandWildernessSlots;
    int m_campPicture = 0;
    int m_treasurePicture = 0;
    QVector<Grid> m_map;
    QVector<EventRecord> m_events;
};
