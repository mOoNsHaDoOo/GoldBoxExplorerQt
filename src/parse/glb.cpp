#include "parse/glb.h"

#include <QFile>

namespace {

constexpr int kHeaderSize = 16;

} // namespace

quint16 GlbFile::read16(const QByteArray &data, int offset) const
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    const quint8 a = static_cast<quint8>(data.at(offset));
    const quint8 b = static_cast<quint8>(data.at(offset + 1));
    return m_bigEndian ? static_cast<quint16>((a << 8) | b)
                       : static_cast<quint16>((b << 8) | a);
}

quint32 GlbFile::read32(const QByteArray &data, int offset) const
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    const quint32 a = static_cast<quint8>(data.at(offset));
    const quint32 b = static_cast<quint8>(data.at(offset + 1));
    const quint32 c = static_cast<quint8>(data.at(offset + 2));
    const quint32 d = static_cast<quint8>(data.at(offset + 3));
    return m_bigEndian ? ((a << 24) | (b << 16) | (c << 8) | d)
                       : ((d << 24) | (c << 16) | (b << 8) | a);
}

bool GlbFile::looksLikeGlb(const QByteArray &data)
{
    if (data.size() < kHeaderSize)
        return false;
    if (data.left(4) != QByteArrayLiteral("GLIB")
        && data.left(4) != QByteArrayLiteral("HLIB"))
        return false;
    const quint32 sizeBe = (static_cast<quint32>(static_cast<quint8>(data.at(4))) << 24)
                           | (static_cast<quint32>(static_cast<quint8>(data.at(5))) << 16)
                           | (static_cast<quint32>(static_cast<quint8>(data.at(6))) << 8)
                           | static_cast<quint32>(static_cast<quint8>(data.at(7)));
    const quint32 sizeLe = static_cast<quint32>(static_cast<quint8>(data.at(4)))
                           | (static_cast<quint32>(static_cast<quint8>(data.at(5))) << 8)
                           | (static_cast<quint32>(static_cast<quint8>(data.at(6))) << 16)
                           | (static_cast<quint32>(static_cast<quint8>(data.at(7))) << 24);
    return sizeBe == static_cast<quint32>(data.size())
           || sizeLe == static_cast<quint32>(data.size());
}

bool GlbFile::load(const QString &path)
{
    m_error.clear();
    m_contentType.clear();
    m_bigEndian = false;
    m_valid = false;
    m_entries = 0;
    m_blocks.clear();
    m_idTable.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();
    if (!looksLikeGlb(data)) {
        m_error = QStringLiteral("Not a GLIB container.");
        return false;
    }

    const quint32 sizeBe = (static_cast<quint32>(static_cast<quint8>(data.at(4))) << 24)
                           | (static_cast<quint32>(static_cast<quint8>(data.at(5))) << 16)
                           | (static_cast<quint32>(static_cast<quint8>(data.at(6))) << 8)
                           | static_cast<quint32>(static_cast<quint8>(data.at(7)));
    m_bigEndian = sizeBe == static_cast<quint32>(data.size());

    m_entries = read16(data, 8);
    const bool hasTable = static_cast<quint8>(data.at(11)) == 1;
    m_contentType = QString::fromLatin1(data.mid(12, 4));

    QVector<quint32> offsets;
    quint32 firstData = 0;

    if (hasTable) {
        const quint32 idTableOffset = read32(data, 16);
        firstData = read32(data, 20);
        if (idTableOffset > static_cast<quint32>(data.size()))
            return false;
        const int count = qMax(0, static_cast<int>(
            (idTableOffset - 24) / 4));
        for (int i = 0; i < count; ++i)
            offsets.append(read32(data, 24 + i * 4));

        const int pairs = qMax(0, static_cast<int>((firstData - idTableOffset) / 4));
        for (int i = 0; i < pairs; ++i) {
            m_idTable.append(read16(data, static_cast<int>(idTableOffset) + i * 4));
            m_idTable.append(read16(data, static_cast<int>(idTableOffset) + i * 4 + 2));
        }

        if (firstData > 0 && firstData < static_cast<quint32>(data.size()))
            offsets.prepend(firstData);
    } else {
        firstData = read32(data, 16);
        const int count = qMax(0, static_cast<int>((firstData - 20) / 4));
        for (int i = 0; i < count; ++i)
            offsets.append(read32(data, 20 + i * 4));
    }

    if (offsets.isEmpty()) {
        m_error = QStringLiteral("GLIB container has no block offsets.");
        return false;
    }

    for (int i = 0; i < offsets.size(); ++i) {
        const quint32 offset = offsets.at(i);
        const quint32 next = (i + 1 < offsets.size())
                                 ? offsets.at(i + 1)
                                 : static_cast<quint32>(data.size());
        if (offset >= next || next > static_cast<quint32>(data.size()))
            continue;
        GlbBlock block;
        block.index = m_blocks.size();
        block.offset = offset;
        block.data = data.mid(static_cast<int>(offset),
                              static_cast<int>(next - offset));
        m_blocks.append(block);
    }

    if (m_blocks.isEmpty()) {
        m_error = QStringLiteral("GLIB block offsets are invalid.");
        return false;
    }

    m_valid = true;
    return true;
}
