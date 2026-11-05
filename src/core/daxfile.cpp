#include "core/daxfile.h"

#include "core/byteutils.h"

#include <QFile>
#include <QFileInfo>

#include <cstring>

bool DaxFile::load(const QString &path)
{
    m_fileName = path;
    m_blocks.clear();
    m_valid = false;
    m_error.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    const QByteArray raw = file.readAll();
    if (raw.size() < 2) {
        m_error = QStringLiteral("File is too small to be a DAX file.");
        return false;
    }

    ByteReader reader(raw);
    const int dataOffset = reader.i16() + 2;
    if (dataOffset < 2 || dataOffset > raw.size()) {
        m_error = QStringLiteral("Invalid DAX data offset.");
        return false;
    }

    constexpr int headerEntrySize = 9;
    struct Header
    {
        int id;
        int offset;
        int rawSize;
        int compressedSize;
    };
    QVector<Header> headers;

    const int headerCount = (dataOffset - 2) / headerEntrySize;
    for (int i = 0; i < headerCount; ++i) {
        Header h;
        h.id = reader.u8();
        h.offset = reader.i32();
        h.rawSize = reader.u16();
        h.compressedSize = reader.u16();
        if (!reader.ok())
            break;
        headers.append(h);
    }

    for (const Header &h : headers) {
        const int start = dataOffset + h.offset;
        if (start < 0 || start > raw.size() || h.compressedSize < 0)
            continue;

        const QByteArray compressed = raw.mid(start, h.compressedSize);
        if (h.rawSize <= 0) {
            // The original stores some blocks uncompressed (rawSize is signed).
            m_blocks.append({path, h.id, compressed});
        } else {
            if (compressed.isEmpty())
                continue;
            QByteArray decoded = decode(compressed, h.rawSize);
            if (decoded.size() < h.rawSize)
                decoded.resize(h.rawSize);
            m_blocks.append({path, h.id, decoded});
        }
    }

    m_valid = true;
    return true;
}

const DaxFileBlock *DaxFile::blockById(int id) const
{
    for (const DaxFileBlock &block : m_blocks) {
        if (block.id == id)
            return &block;
    }
    return nullptr;
}

QByteArray DaxFile::allBytes() const
{
    QByteArray result;
    for (const DaxFileBlock &block : m_blocks)
        result += block.data;
    return result;
}

QByteArray DaxFile::decode(const QByteArray &input, int outputSize)
{
    QByteArray output(qMax(0, outputSize), '\0');
    int inputIndex = 0;
    int outputIndex = 0;

    while (inputIndex < input.size() && outputIndex < output.size()) {
        const qint8 runLength = static_cast<qint8>(input.at(inputIndex));

        if (runLength >= 0) {
            const int count = runLength + 1;
            if (inputIndex + count >= input.size())
                break;
            const int copyCount = qMin(count, output.size() - outputIndex);
            std::memcpy(output.data() + outputIndex, input.constData() + inputIndex + 1,
                        static_cast<size_t>(copyCount));
            inputIndex += runLength + 2;
            outputIndex += count;
        } else {
            const int count = -runLength;
            if (inputIndex + 1 >= input.size())
                break;
            const char value = input.at(inputIndex + 1);
            const int copyCount = qMin(count, output.size() - outputIndex);
            std::memset(output.data() + outputIndex, static_cast<unsigned char>(value),
                        static_cast<size_t>(copyCount));
            inputIndex += 2;
            outputIndex += count;
        }
    }
    return output;
}
