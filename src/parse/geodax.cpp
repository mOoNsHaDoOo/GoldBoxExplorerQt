#include "parse/geodax.h"

#include "core/byteutils.h"
#include "parse/geodax_names.h"

GeoDaxFile::GeoDaxFile(const QString &path)
    : m_file(path)
{
    if (!m_file.isValid())
        return;

    m_game = GameDetector::determineGameFrom(path);

    for (const DaxFileBlock &block : m_file.blocks()) {
        if (block.data.size() < 1026)
            continue;

        GeoMapRecord map;
        map.daxId = block.id;
        map.name = mapNameFor(m_game, block.id);
        map.walls.reserve(256);

        for (int i = 0; i < 256; ++i) {
            const int row = i >> 4;
            const int col = i & 0x0F;

            const quint8 ne = byteAt(block.data, i + 2);
            const quint8 sw = byteAt(block.data, i + 258);

            GeoWallRecord wall;
            wall.row = row;
            wall.column = col;
            wall.north = (ne >> 4) & 0x0F;
            wall.east = ne & 0x0F;
            wall.south = (sw >> 4) & 0x0F;
            wall.west = sw & 0x0F;
            wall.event = byteAt(block.data, i + 514);
            wall.door = byteAt(block.data, i + 770);
            map.walls.append(wall);
        }
        m_maps.append(map);
    }
}
