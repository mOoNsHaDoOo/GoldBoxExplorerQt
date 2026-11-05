#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

// Generic reader for the "GLIB" containers used by the later Gold Box games.
// The DOS files are little-endian; the Amiga conversions use the same layout
// with big-endian fields.  The content type selects how the blocks are used:
// DATA (ECL/GEO/ITEM/MON*), TEXT (STRINGS), DIG8 (SOUNDS), TILE/GLIB (TLB).
struct GlbBlock
{
    int index = 0;
    quint32 offset = 0;
    QByteArray data;
};

class GlbFile
{
public:
    GlbFile() = default;
    explicit GlbFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }

    bool isBigEndian() const { return m_bigEndian; }
    QString contentType() const { return m_contentType; }
    int entryCount() const { return m_entries; }
    const QVector<GlbBlock> &blocks() const { return m_blocks; }
    const QVector<quint16> &idTable() const { return m_idTable; }

    static bool looksLikeGlb(const QByteArray &data);

private:
    quint16 read16(const QByteArray &data, int offset) const;
    quint32 read32(const QByteArray &data, int offset) const;

    QString m_error;
    QString m_contentType;
    bool m_bigEndian = false;
    bool m_valid = false;
    int m_entries = 0;
    QVector<GlbBlock> m_blocks;
    QVector<quint16> m_idTable;
};
