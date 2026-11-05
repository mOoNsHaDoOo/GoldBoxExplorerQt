#pragma once

#include "core/daxfile.h"

#include <QImage>
#include <QString>
#include <QVector>

struct WallSet
{
    int blockId = 0;
    QVector<QImage> walls;
};

struct WallDef
{
    int rows = 0;
    int cols = 0;
    QByteArray data;
};

class DaxWallDefFile
{
public:
    explicit DaxWallDefFile(const QString &path);

    bool isValid() const { return m_file.isValid(); }
    const QVector<WallSet> &wallSets() const { return m_wallSets; }
    QVector<QImage> allBitmaps() const;
    QVector<int> bitmapIds() const;
    QString fileName() const { return m_file.fileName(); }

private:
    void processBlock(const DaxFileBlock &block);
    QVector<QImage> assembleWallSet(const DaxFileBlock &block, const QString &fileName);
    QVector<QImage> load8x8Bitmaps(const DaxFileBlock &block, const QString &fileName,
                                   const QString &tileFileName);
    static QVector<WallDef> loadWallDefs(const DaxFileBlock &block);

    static QImage bitmapZero();
    static QString universalTileFileName(const QString &fileName);
    static QString resolveExisting(const QString &path);
    static int bitmapCount(int blockId, const QString &fileName);
    static void loadBitmaps(int blockId, const QString &fileName, QVector<QImage> &target);

    DaxFile m_file;
    QVector<WallSet> m_wallSets;
};
