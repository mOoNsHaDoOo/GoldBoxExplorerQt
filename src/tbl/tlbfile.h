#pragma once

#include <QByteArray>
#include <QImage>
#include <QPoint>
#include <QRgb>
#include <QString>
#include <QVector>

class ByteReader;

struct TlbImageHeader
{
    quint16 imageHeight = 0;
    qint16 verticalOffset = 0;
    qint16 horizontalOffset = 0;
    int imageWidth = 0;
    quint8 drawingMethod = 0;
};

class TlbFile
{
public:
    explicit TlbFile(const QString &path) { load(path); }

    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    QByteArray rawBytes() const { return m_bytes; }

    void loadBitmaps();
    const QVector<QImage> &bitmaps() const { return m_bitmaps; }
    QVector<QPoint> imageOffsets() const;

    struct Header
    {
        QString fileType;
        quint32 fileSize = 0;
        quint16 entries = 0;
        quint8 unused = 0;
        bool hasTable = false;
        QString contentType;
    };

private:
    bool load(const QString &path);
    void readHeader(ByteReader &reader);
    int getImageCount(ByteReader &reader);
    void readMasterLibrary(ByteReader &reader);
    void readTileLibrary(ByteReader &reader, int imageCount, quint32 startingPoint);
    void readColorTable(ByteReader &reader, quint32 startingPoint);
    void readColorTableHeader(ByteReader &reader);
    void readColorTableData(ByteReader &reader);
    void findAndLoadExternalColorTable();
    void loadExternalColorTable(const QString &path);

    QImage createBitmap(const TlbImageHeader &header, ByteReader &reader);
    QImage createBitmapFromBuffer(const TlbImageHeader &header, const QByteArray &buffer);
    QImage createAmigaPlanar(const TlbImageHeader &header, ByteReader &reader, int planes);
    static QByteArray deinterlacePixels(const QByteArray &buffer);

    quint16 u16(ByteReader &reader) const;
    quint32 u32(ByteReader &reader) const;
    qint16 i16(ByteReader &reader) const;

    QString m_fullPath;
    QByteArray m_bytes;
    QString m_error;
    bool m_valid = false;
    bool m_bigEndian = false;
    int m_derivedColorCount = 0;

    Header m_header;
    quint32 m_colorDataOffset = 0;
    quint32 m_imageIdTableOffset = 0;
    quint16 m_colorFirst = 0;
    quint16 m_colorCount = 0;
    QVector<quint32> m_offsets;
    QVector<QImage> m_bitmaps;
    QVector<QPoint> m_imageOffsets;
    QVector<QRgb> m_palette;
    bool m_combatIcon = false;
};
