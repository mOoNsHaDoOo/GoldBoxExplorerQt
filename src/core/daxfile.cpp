#include "core/daxfile.h"

#include "core/byteutils.h"

#include <QFile>
#include <QFileInfo>

#include <cstring>

namespace {

quint16 readBe16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}

quint32 readBe32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

// ByteKiller unpacker used by the Amiga Gold Box games. The bit stream is
// read backwards from the end of the block and the output is written
// backwards from its end. The last 12 bytes are:
//   unpacked length, running checksum, first bit buffer (all big-endian u32).
bool byteKillerUnpack(const QByteArray &block, int expectedSize,
                      QByteArray &output, QString *error)
{
    if (block.size() < 12) {
        if (error)
            *error = QStringLiteral("ByteKiller block is shorter than its trailer.");
        return false;
    }

    int readAt = block.size();
    auto longword = [&]() -> quint32 {
        readAt -= 4;
        if (readAt < 0)
            return 0;
        return readBe32(block, readAt);
    };

    const quint32 length = longword();
    quint32 checksum = longword();
    quint32 buffer = longword();
    checksum ^= buffer;

    if (length == 0 || length > 16 * 1024 * 1024)
        return false;
    if (expectedSize > 0 && static_cast<quint32>(expectedSize) != length)
        return false;

    output = QByteArray(static_cast<int>(length), '\0');
    int at = static_cast<int>(length);
    bool failed = false;

    auto bit = [&]() -> int {
        if (failed)
            return 0;
        quint32 value = buffer & 1;
        buffer >>= 1;
        if (buffer == 0) {
            buffer = longword();
            if (readAt < 0) {
                failed = true;
                return 0;
            }
            checksum ^= buffer;
            value = buffer & 1;
            buffer = (buffer >> 1) | 0x80000000u;
        }
        return static_cast<int>(value);
    };

    auto bits = [&](int count) -> quint32 {
        quint32 value = 0;
        for (int i = 0; i < count; ++i)
            value = (value << 1) | static_cast<quint32>(bit());
        return value;
    };

    auto literals = [&](int count) {
        for (int i = 0; i < count && !failed; ++i) {
            --at;
            if (at < 0) {
                failed = true;
                return;
            }
            output[at] = static_cast<char>(bits(8));
        }
    };

    auto copy = [&](int count, int offset) {
        for (int i = 0; i < count && !failed; ++i) {
            --at;
            const int source = at + offset;
            if (at < 0 || source < 0 || source >= output.size()) {
                failed = true;
                return;
            }
            output[at] = output.at(source);
        }
    };

    while (at > 0 && !failed) {
        if (bit()) {
            const quint32 tag = bits(2);
            if (tag < 2) {
                copy(static_cast<int>(tag) + 3, static_cast<int>(bits(9 + tag)));
            } else if (tag == 2) {
                const int count = static_cast<int>(bits(8)) + 1;
                copy(count, static_cast<int>(bits(12)));
            } else {
                literals(static_cast<int>(bits(8)) + 9);
            }
        } else if (bit()) {
            copy(2, static_cast<int>(bits(8)));
        } else {
            literals(static_cast<int>(bits(3)) + 1);
        }
    }

    if (failed || at != 0 || checksum != 0)
        return false;
    return true;
}

bool parseAmigaDax(const QByteArray &raw, const QString &path,
                   QVector<DaxFileBlock> &blocks, QString *error)
{
    if (raw.size() < 12)
        return false;
    const int indexSize = readBe16(raw, 0);
    if (indexSize < 10 || indexSize % 10 != 0 || 2 + indexSize > raw.size())
        return false;

    const int count = indexSize / 10;
    if (count > 4096)
        return false;
    const int base = 2 + indexSize;
    const int payload = raw.size() - base;

    struct Entry { int id; quint32 offset; int compressed; int rawSize; };
    QVector<Entry> entries;
    for (int i = 0; i < count; ++i) {
        const int at = 2 + i * 10;
        Entry entry;
        entry.id = readBe16(raw, at);
        entry.offset = readBe32(raw, at + 2);
        entry.compressed = readBe16(raw, at + 6);
        entry.rawSize = readBe16(raw, at + 8);
        if (entry.compressed <= 0 || entry.rawSize <= 0
            || entry.offset + entry.compressed > static_cast<quint32>(payload))
            return false;
        entries.append(entry);
    }

    QVector<DaxFileBlock> result;
    bool first = true;
    for (const Entry &entry : entries) {
        const QByteArray compressed = raw.mid(base + entry.offset, entry.compressed);
        QByteArray decoded;
        if (!byteKillerUnpack(compressed, entry.rawSize, decoded, error)) {
            if (first)
                return false;
            continue;
        }
        result.append({path, entry.id, decoded});
        first = false;
    }
    if (result.isEmpty())
        return false;
    blocks = result;
    return true;
}

} // namespace

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

    // Try the Amiga layout first: big-endian index and ByteKiller blocks.
    // The ByteKiller sanity check avoids misreading a DOS DAX as Amiga.
    {
        QVector<DaxFileBlock> amigaBlocks;
        if (parseAmigaDax(raw, path, amigaBlocks, nullptr)
            && !amigaBlocks.isEmpty()) {
            m_blocks = amigaBlocks;
            m_amiga = true;
            m_valid = true;
            return true;
        }
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
