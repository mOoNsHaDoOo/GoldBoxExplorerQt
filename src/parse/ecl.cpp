#include "parse/ecl.h"

#include "core/byteutils.h"

EclFile::EclFile(const QString &path)
    : m_file(path)
{
    if (!m_file.isValid())
        return;

    for (const DaxFileBlock &block : m_file.blocks()) {
        EclBlock info;
        info.id = block.id;
        info.data = block.data;
        info.strings = extractStrings(block.data);
        m_blocks.append(info);
    }
}

QStringList EclFile::extractStrings(const QByteArray &data)
{
    QStringList strings;
    for (int i = 0; i < data.size() - 2; ++i) {
        if (byteAt(data, i) != 0x80)
            continue;

        const int len = byteAt(data, i + 1);
        if (i + 1 + len >= data.size())
            continue;

        const QByteArray block = data.mid(i + 2, len);
        const QString text = decompressString(block).trimmed();
        if (isReadableText(text))
            strings.append(text);
    }
    return strings;
}

QString EclFile::decompressString(const QByteArray &data)
{
    QString result;
    int state = 1;
    quint32 lastByte = 0;

    for (char c : data) {
        const quint32 thisByte = static_cast<quint8>(c);
        auto inflate = [](quint32 value) -> QChar {
            if (value <= 0x1F)
                value += 0x40;
            return QChar(static_cast<ushort>(value));
        };

        switch (state) {
        case 1: {
            const quint32 curr = (thisByte >> 2) & 0x3F;
            if (curr != 0)
                result.append(inflate(curr));
            state = 2;
            break;
        }
        case 2: {
            const quint32 curr = ((lastByte << 4) | (thisByte >> 4)) & 0x3F;
            if (curr != 0)
                result.append(inflate(curr));
            state = 3;
            break;
        }
        case 3: {
            const quint32 curr1 = ((lastByte << 2) | (thisByte >> 6)) & 0x3F;
            if (curr1 != 0)
                result.append(inflate(curr1));
            const quint32 curr2 = thisByte & 0x3F;
            if (curr2 != 0)
                result.append(inflate(curr2));
            state = 1;
            break;
        }
        }
        lastByte = thisByte;
    }
    return result;
}

bool EclFile::isReadableText(const QString &text)
{
    static const QString invalid = QStringLiteral("_=[]()%<$*&\\/^>");
    int noSpace = 0;

    if (text.trimmed().length() <= 3)
        return false;
    if (!text.isEmpty() && text.at(0).isDigit())
        return false;
    if (!text.isEmpty() && text.at(0).isPunct())
        return false;

    for (const QChar ch : text) {
        if (ch.isSpace())
            noSpace = 0;
        else
            ++noSpace;
        if (noSpace > 15)
            return false;
        if (invalid.contains(ch))
            return false;
    }
    return true;
}

QString EclFile::allText() const
{
    QStringList all;
    for (const EclBlock &block : m_blocks)
        all += block.strings;
    return all.join(QLatin1Char('\n'));
}
