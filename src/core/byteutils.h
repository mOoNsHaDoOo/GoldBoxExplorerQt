#pragma once

#include <QByteArray>
#include <QtEndian>
#include <cstring>

// Small little-endian reader used by all the legacy Gold Box parsers.
// Out-of-range reads return 0 and set ok() to false instead of throwing.
class ByteReader
{
public:
    explicit ByteReader(const QByteArray &data) : m_data(data) {}

    bool ok() const { return m_ok; }
    int pos() const { return m_pos; }
    int size() const { return m_data.size(); }
    int remaining() const { return m_data.size() - m_pos; }
    bool atEnd() const { return m_pos >= m_data.size(); }
    const QByteArray &data() const { return m_data; }

    void seek(int pos)
    {
        if (pos < 0 || pos > m_data.size()) {
            m_ok = false;
            m_pos = qBound(0, pos, m_data.size());
            return;
        }
        m_pos = pos;
    }

    void skip(int count) { seek(m_pos + count); }

    quint8 u8()
    {
        if (!has(1))
            return 0;
        return static_cast<quint8>(m_data.at(m_pos++));
    }

    qint8 i8() { return static_cast<qint8>(u8()); }

    quint16 u16() { return static_cast<quint16>(readLE(2)); }
    qint16 i16() { return static_cast<qint16>(u16()); }
    quint32 u32() { return static_cast<quint32>(readLE(4)); }
    qint32 i32() { return static_cast<qint32>(u32()); }

    QByteArray bytes(int count)
    {
        if (count <= 0)
            return {};
        if (!has(count)) {
            const int available = qMax(0, m_data.size() - m_pos);
            QByteArray part = m_data.mid(m_pos, available);
            m_pos = m_data.size();
            m_ok = false;
            return part;
        }
        QByteArray part = m_data.mid(m_pos, count);
        m_pos += count;
        return part;
    }

    // Returns a NUL-terminated ASCII/Latin-1 string, like .NET's new string(char[]).
    QString fixedString(int length)
    {
        QByteArray raw = bytes(length);
        const int nul = raw.indexOf('\0');
        if (nul >= 0)
            raw.truncate(nul);
        return QString::fromLatin1(raw);
    }

private:
    bool has(int count) const
    {
        const bool result = m_pos >= 0 && count >= 0 && m_pos + count <= m_data.size();
        if (!result)
            m_ok = false;
        return result;
    }

    quint32 readLE(int count)
    {
        if (!has(count))
            return 0;
        quint32 value = 0;
        for (int i = 0; i < count; ++i)
            value |= static_cast<quint32>(static_cast<quint8>(m_data.at(m_pos + i))) << (8 * i);
        m_pos += count;
        return value;
    }

    const QByteArray &m_data;
    int m_pos = 0;
    mutable bool m_ok = true;
};

inline quint16 readU16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>(static_cast<quint8>(data.at(offset))
                                | (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8));
}

inline qint16 readI16(const QByteArray &data, int offset)
{
    return static_cast<qint16>(readU16(data, offset));
}

inline quint32 readU32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return static_cast<quint32>(static_cast<quint8>(data.at(offset)))
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
}

inline quint8 byteAt(const QByteArray &data, int offset)
{
    if (offset < 0 || offset >= data.size())
        return 0;
    return static_cast<quint8>(data.at(offset));
}
