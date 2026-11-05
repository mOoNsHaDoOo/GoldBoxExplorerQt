#pragma once

#include "core/daxfile.h"
#include "core/game.h"

#include <QString>
#include <QVector>

struct GeoWallRecord
{
    int row = 0;
    int column = 0;
    int north = 0;
    int east = 0;
    int south = 0;
    int west = 0;
    quint8 event = 0;
    int door = 0;
};

struct GeoMapRecord
{
    QString name;
    int daxId = 0;
    QVector<GeoWallRecord> walls;
};

class GeoDaxFile
{
public:
    explicit GeoDaxFile(const QString &path);

    bool isValid() const { return m_file.isValid(); }
    QString errorString() const { return m_file.errorString(); }
    const QVector<GeoMapRecord> &maps() const { return m_maps; }
    GameList game() const { return m_game; }

private:
    DaxFile m_file;
    QVector<GeoMapRecord> m_maps;
    GameList m_game = GameList::Unknown;
};
