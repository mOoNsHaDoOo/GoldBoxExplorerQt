#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

struct DaaBlock
{
    int id = 0;
    quint32 offset = 0;
    quint16 rawSize = 0;
    quint16 compressedSize = 0;
    QByteArray data; // decoded block
};

// Container used by the Amiga Gold Box ports. The header is big-endian:
//   u16 dataOffset
//   { u8 id, u32 offset, u16 rawSize, u16 compressedSize } * ((dataOffset-2)/9)
// Blocks are compressed with the same run-length codec as DOS DAX.
class DaaFile
{
public:
    DaaFile() = default;
    explicit DaaFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    const QVector<DaaBlock> &blocks() const { return m_blocks; }
    quint16 dataOffset() const { return m_dataOffset; }

private:
    QString m_path;
    QString m_error;
    quint16 m_dataOffset = 0;
    QVector<DaaBlock> m_blocks;
    bool m_valid = false;
};

// Decodes the image records inside a DAA container. Returns the images and
// fills imageIds with the corresponding DAA block id.
QVector<QImage> decodeDaaImages(const DaaFile &file, QVector<int> *imageIds = nullptr,
                                QString *error = nullptr);
