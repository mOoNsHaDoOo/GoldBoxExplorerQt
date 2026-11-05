#pragma once

#include "core/daxfile.h"

#include <QImage>
#include <QVector>

class DaxImageFile
{
public:
    explicit DaxImageFile(const QString &path);

    const DaxFile &dax() const { return m_file; }
    const QVector<QImage> &bitmaps() const { return m_bitmaps; }
    const QVector<int> &bitmapIds() const { return m_bitmapIds; }

private:
    DaxFile m_file;
    QVector<QImage> m_bitmaps;
    QVector<int> m_bitmapIds;
};
