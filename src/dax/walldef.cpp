#include "dax/walldef.h"

#include "core/byteutils.h"
#include "dax/daximagefile.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>

DaxWallDefFile::DaxWallDefFile(const QString &path)
    : m_file(path)
{
    if (!m_file.isValid())
        return;
    for (const DaxFileBlock &block : m_file.blocks())
        processBlock(block);
}

void DaxWallDefFile::processBlock(const DaxFileBlock &block)
{
    WallSet set;
    set.blockId = block.id;
    set.walls = assembleWallSet(block, m_file.fileName());
    m_wallSets.append(set);
}

QVector<QImage> DaxWallDefFile::allBitmaps() const
{
    QVector<QImage> result;
    for (const WallSet &set : m_wallSets)
        result += set.walls;
    return result;
}

QVector<int> DaxWallDefFile::bitmapIds() const
{
    QVector<int> result;
    for (const WallSet &set : m_wallSets) {
        for (int i = 0; i < set.walls.size(); ++i)
            result.append(set.blockId);
    }
    return result;
}

QVector<QImage> DaxWallDefFile::assembleWallSet(const DaxFileBlock &block, const QString &fileName)
{
    const QFileInfo info(fileName);
    QString tileBase = info.fileName();
    tileBase.replace(QStringLiteral("walldef"), QStringLiteral("8x8d"), Qt::CaseInsensitive);
    const QString tileFileName = resolveExisting(info.absoluteDir().filePath(tileBase));

    const QVector<QImage> tiles = load8x8Bitmaps(block, fileName, tileFileName);
    const QVector<WallDef> defs = loadWallDefs(block);

    QVector<QImage> walls;
    for (const WallDef &def : defs) {
        if (def.rows <= 0 || def.cols <= 0)
            continue;

        QImage wall(def.cols * 8, def.rows * 8, QImage::Format_ARGB32);
        wall.fill(Qt::transparent);
        QPainter painter(&wall);
        for (int r = 0; r < def.rows; ++r) {
            for (int c = 0; c < def.cols; ++c) {
                const int index = byteAt(def.data, r * def.cols + c);
                if (index < 0 || index >= tiles.size())
                    continue;
                const QImage &tile = tiles.at(index);
                if (!tile.isNull())
                    painter.drawImage(c * tile.width(), r * tile.height(), tile);
            }
        }
        painter.end();
        walls.append(wall);
    }
    return walls;
}

QVector<QImage> DaxWallDefFile::load8x8Bitmaps(const DaxFileBlock &block,
                                               const QString &fileName,
                                               const QString &tileFileName)
{
    const int count = bitmapCount(block.id, tileFileName);
    QVector<QImage> bitmaps;
    bitmaps.append(bitmapZero());

    if (count >= 255) {
        if (count == 256)
            bitmaps.removeAt(0);
        loadBitmaps(block.id, tileFileName, bitmaps);
    } else {
        loadBitmaps(203, universalTileFileName(fileName), bitmaps);

        const int wallSetCount = block.data.size() / 780;
        if (wallSetCount >= 1)
            loadBitmaps(block.id, tileFileName, bitmaps);

        if (bitmaps.size() < 255 && wallSetCount > 1) {
            int baseBlockId = 10 * block.id;
            if (block.id == 0)
                baseBlockId = 100;

            loadBitmaps(baseBlockId + 1, tileFileName, bitmaps);
            loadBitmaps(baseBlockId + 2, tileFileName, bitmaps);
            if (wallSetCount == 3)
                loadBitmaps(baseBlockId + 3, tileFileName, bitmaps);
        }
    }

    if (!bitmaps.isEmpty())
        bitmaps[0] = bitmapZero();
    return bitmaps;
}

QVector<WallDef> DaxWallDefFile::loadWallDefs(const DaxFileBlock &block)
{
    static const int idxOffset[] = {0, 2, 6, 10, 22, 38, 54, 110, 132, 154, 1};
    static const int colCount[] = {1, 1, 1, 3, 2, 2, 7, 2, 2, 1};
    static const int rowCount[] = {2, 4, 4, 4, 8, 8, 8, 11, 11, 2};
    constexpr int wallSliceSize = 156;

    QVector<WallDef> result;
    const int wallCount = block.data.size() / wallSliceSize;
    for (int wallNumber = 0; wallNumber < wallCount; ++wallNumber) {
        for (int view = 0; view < 10; ++view) {
            const int rows = rowCount[view];
            const int cols = colCount[view];
            int index = idxOffset[view];
            WallDef def;
            def.rows = rows;
            def.cols = cols;
            def.data.resize(rows * cols);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c)
                    def.data[r * cols + c] = static_cast<char>(
                        byteAt(block.data, wallSliceSize * wallNumber + index++));
            }
            result.append(def);
        }
    }
    return result;
}

QImage DaxWallDefFile::bitmapZero()
{
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(qRgb(255, 82, 255));
    return image;
}

QString DaxWallDefFile::universalTileFileName(const QString &fileName)
{
    const QDir dir = QFileInfo(fileName).absoluteDir();
    QString first = resolveExisting(dir.filePath(QStringLiteral("8x8d1.dax")));
    if (bitmapCount(203, first) == 0)
        first = resolveExisting(dir.filePath(QStringLiteral("8x8d.dax")));
    return first;
}

QString DaxWallDefFile::resolveExisting(const QString &path)
{
    if (QFileInfo::exists(path))
        return path;

    const QFileInfo info(path);
    const QDir dir = info.absoluteDir();
    const QString target = info.fileName();
    const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.compare(target, Qt::CaseInsensitive) == 0)
            return dir.filePath(entry);
    }
    return path;
}

int DaxWallDefFile::bitmapCount(int blockId, const QString &fileName)
{
    const QString resolved = resolveExisting(fileName);
    if (!QFileInfo::exists(resolved))
        return 0;

    DaxImageFile file(resolved);
    int count = 0;
    for (int id : file.bitmapIds()) {
        if (id == blockId)
            ++count;
    }
    return count;
}

void DaxWallDefFile::loadBitmaps(int blockId, const QString &fileName, QVector<QImage> &target)
{
    const QString resolved = resolveExisting(fileName);
    if (!QFileInfo::exists(resolved))
        return;

    DaxImageFile file(resolved);
    for (int i = 0; i < file.bitmaps().size(); ++i) {
        if (file.bitmapIds().value(i, -1) == blockId)
            target.append(file.bitmaps().at(i));
    }
}
