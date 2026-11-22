#include "core/zip.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <array>

namespace {

// ---------------------------------------------------------------- deflate

constexpr int kMaxBits = 15;

struct BitInput
{
    const quint8 *data;
    qint64 size;
    qint64 pos = 0;
    quint32 buffer = 0;
    int count = 0;
    bool error = false;

    int bits(int need)
    {
        quint32 value = buffer;
        while (count < need) {
            if (pos >= size) {
                error = true;
                return 0;
            }
            value |= static_cast<quint32>(data[pos++]) << count;
            count += 8;
        }
        buffer = value >> need;
        count -= need;
        return static_cast<int>(value & ((1u << need) - 1));
    }
};

struct Huffman
{
    std::array<short, kMaxBits + 1> count{};
    std::array<short, 320> symbol{};
};

// Canonical code from the code lengths; returns < 0 for an over-subscribed
// set, > 0 for an incomplete one (allowed for single-code distance sets).
int build(Huffman &h, const short *lengths, int n)
{
    h.count.fill(0);
    for (int s = 0; s < n; ++s)
        ++h.count[lengths[s]];
    if (h.count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0)
            return left;
    }
    std::array<short, kMaxBits + 1> offsets{};
    for (int len = 1; len < kMaxBits; ++len)
        offsets[len + 1] = static_cast<short>(offsets[len] + h.count[len]);
    for (int s = 0; s < n; ++s) {
        if (lengths[s] != 0)
            h.symbol[offsets[lengths[s]]++] = static_cast<short>(s);
    }
    return left;
}

int decodeSymbol(BitInput &in, const Huffman &h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= in.bits(1);
        if (in.error)
            return -1;
        const int count = h.count[len];
        if (code - count < first)
            return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const short kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                             193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                             6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateCodes(BitInput &in, QByteArray &out, const Huffman &lengths, const Huffman &distances)
{
    for (;;) {
        int symbol = decodeSymbol(in, lengths);
        if (symbol < 0)
            return false;
        if (symbol < 256) {
            out.append(static_cast<char>(symbol));
        } else if (symbol == 256) {
            return true;
        } else {
            symbol -= 257;
            if (symbol >= 29)
                return false;
            const int length = kLengthBase[symbol] + in.bits(kLengthExtra[symbol]);
            const int d = decodeSymbol(in, distances);
            if (d < 0 || d >= 30)
                return false;
            const int distance = kDistBase[d] + in.bits(kDistExtra[d]);
            if (in.error || distance > out.size())
                return false;
            const qint64 from = out.size() - distance;
            for (int i = 0; i < length; ++i)
                out.append(out.at(from + i));
        }
    }
}

bool inflateStored(BitInput &in, QByteArray &out)
{
    in.buffer = 0;
    in.count = 0;
    if (in.pos + 4 > in.size)
        return false;
    const int len = in.data[in.pos] | (in.data[in.pos + 1] << 8);
    const int nlen = in.data[in.pos + 2] | (in.data[in.pos + 3] << 8);
    in.pos += 4;
    if (len != (~nlen & 0xFFFF) || in.pos + len > in.size)
        return false;
    out.append(reinterpret_cast<const char *>(in.data + in.pos), len);
    in.pos += len;
    return true;
}

bool inflateFixed(BitInput &in, QByteArray &out)
{
    static Huffman lengths, distances;
    static bool ready = false;
    if (!ready) {
        short l[288];
        for (int s = 0; s < 144; ++s) l[s] = 8;
        for (int s = 144; s < 256; ++s) l[s] = 9;
        for (int s = 256; s < 280; ++s) l[s] = 7;
        for (int s = 280; s < 288; ++s) l[s] = 8;
        build(lengths, l, 288);
        short d[30];
        for (short &v : d) v = 5;
        build(distances, d, 30);
        ready = true;
    }
    return inflateCodes(in, out, lengths, distances);
}

bool inflateDynamic(BitInput &in, QByteArray &out)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = in.bits(5) + 257;
    const int ndist = in.bits(5) + 1;
    const int ncode = in.bits(4) + 4;
    if (in.error || nlen > 286 || ndist > 30)
        return false;
    short lengths[320] = {};
    for (int i = 0; i < ncode; ++i)
        lengths[order[i]] = static_cast<short>(in.bits(3));
    Huffman codeTable;
    if (build(codeTable, lengths, 19) != 0)
        return false;
    int index = 0;
    while (index < nlen + ndist) {
        int symbol = decodeSymbol(in, codeTable);
        if (symbol < 0)
            return false;
        if (symbol < 16) {
            lengths[index++] = static_cast<short>(symbol);
            continue;
        }
        short value = 0;
        int repeat = 0;
        if (symbol == 16) {
            if (index == 0)
                return false;
            value = lengths[index - 1];
            repeat = 3 + in.bits(2);
        } else if (symbol == 17) {
            repeat = 3 + in.bits(3);
        } else {
            repeat = 11 + in.bits(7);
        }
        if (in.error || index + repeat > nlen + ndist)
            return false;
        while (repeat--)
            lengths[index++] = value;
    }
    if (lengths[256] == 0)
        return false;
    Huffman lengthTable, distanceTable;
    const int l = build(lengthTable, lengths, nlen);
    if (l < 0 || (l > 0 && nlen - lengthTable.count[0] != 1))
        return false;
    const int d = build(distanceTable, lengths + nlen, ndist);
    if (d < 0 || (d > 0 && ndist - distanceTable.count[0] != 1))
        return false;
    return inflateCodes(in, out, lengthTable, distanceTable);
}

// ------------------------------------------------------------------- ZIP

constexpr quint32 kLocalHeader = 0x04034b50;
constexpr quint32 kCentralHeader = 0x02014b50;
constexpr quint32 kEndRecord = 0x06054b50;
constexpr quint32 kZip64EndRecord = 0x06064b50;
constexpr quint32 kZip64Locator = 0x07064b50;

quint16 le16(const QByteArray &d, qint64 o)
{
    if (o < 0 || o + 2 > d.size())
        return 0;
    return static_cast<quint16>(static_cast<quint8>(d.at(o)) | (static_cast<quint8>(d.at(o + 1)) << 8));
}

quint32 le32(const QByteArray &d, qint64 o)
{
    return static_cast<quint32>(le16(d, o)) | (static_cast<quint32>(le16(d, o + 2)) << 16);
}

quint64 le64(const QByteArray &d, qint64 o)
{
    return static_cast<quint64>(le32(d, o)) | (static_cast<quint64>(le32(d, o + 4)) << 32);
}

QString cp437(const QByteArray &raw)
{
    static const QString high = QString::fromUtf8(
        "ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»"
        "░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
        "αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ");
    QString out;
    out.reserve(raw.size());
    for (char c : raw) {
        const quint8 b = static_cast<quint8>(c);
        out.append(b < 128 || high.size() != 128 ? QChar(b) : high.at(b - 128));
    }
    return out;
}

// Relative, '/' separated, without empty, "." or ".." parts.
QString cleanPath(const QString &raw)
{
    QStringList parts;
    for (const QString &part : QString(raw).replace(QLatin1Char('\\'), QLatin1Char('/'))
                                   .split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QStringLiteral(".") || part == QStringLiteral(".."))
            continue;
        QString p = part;
        p.replace(QLatin1Char(':'), QLatin1Char('_'));
        parts.append(p);
    }
    return parts.join(QLatin1Char('/'));
}

QString methodName(int method)
{
    switch (method) {
    case 1: return QStringLiteral("Shrink");
    case 6: return QStringLiteral("Implode");
    case 9: return QStringLiteral("Deflate64");
    case 12: return QStringLiteral("bzip2");
    case 14: return QStringLiteral("LZMA");
    case 93: return QStringLiteral("Zstandard");
    case 95: return QStringLiteral("xz");
    case 98: return QStringLiteral("PPMd");
    case 99: return QStringLiteral("AES encryption");
    default: return QStringLiteral("method %1").arg(method);
    }
}

} // namespace

bool inflateRaw(const QByteArray &input, QByteArray &output, qint64 expectedSize)
{
    output.clear();
    if (expectedSize > 0 && expectedSize < 0x7FFFFFFF)
        output.reserve(static_cast<qsizetype>(expectedSize));
    BitInput in{reinterpret_cast<const quint8 *>(input.constData()), input.size()};
    int last = 0;
    do {
        last = in.bits(1);
        const int type = in.bits(2);
        if (in.error)
            return false;
        bool ok = false;
        if (type == 0)
            ok = inflateStored(in, output);
        else if (type == 1)
            ok = inflateFixed(in, output);
        else if (type == 2)
            ok = inflateDynamic(in, output);
        if (!ok)
            return false;
        if (expectedSize >= 0 && output.size() > expectedSize)
            return false;
    } while (!last);
    return true;
}

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (char c : data)
        crc = table[(crc ^ static_cast<quint8>(c)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool ZipArchive::looksLikeZip(const QString &path)
{
    const QFileInfo info(path);
    if (info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0 || info.size() < 22)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(4);
    return head == QByteArrayLiteral("PK\x03\x04") || head == QByteArrayLiteral("PK\x05\x06");
}

bool ZipArchive::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        m_valid = false;
        return false;
    }
    return parse(file.readAll());
}

bool ZipArchive::parse(const QByteArray &data)
{
    m_data = data;
    m_entries.clear();
    m_warnings.clear();
    m_error.clear();
    m_valid = false;

    // End of central directory: the last record, within 64 KB + 22 bytes.
    qint64 end = -1;
    for (qint64 o = data.size() - 22; o >= 0 && o >= data.size() - 22 - 0xFFFF; --o) {
        if (le32(data, o) == kEndRecord) {
            end = o;
            break;
        }
    }
    if (end < 0) {
        m_error = QStringLiteral("No ZIP central directory (not a ZIP file, or a split archive).");
        return false;
    }
    if (le16(data, end + 4) != 0 || le16(data, end + 6) != 0) {
        m_error = QStringLiteral("Split (multi-volume) ZIP archives are not supported.");
        return false;
    }
    quint64 count = le16(data, end + 10);
    quint64 directorySize = le32(data, end + 12);
    quint64 directoryOffset = le32(data, end + 16);

    // ZIP64: locator just before the end record.
    if (end >= 20 && le32(data, end - 20) == kZip64Locator) {
        const qint64 record = static_cast<qint64>(le64(data, end - 12));
        if (le32(data, record) == kZip64EndRecord) {
            count = le64(data, record + 32);
            directorySize = le64(data, record + 40);
            directoryOffset = le64(data, record + 48);
        }
    }
    if (directoryOffset + directorySize > static_cast<quint64>(data.size())) {
        m_error = QStringLiteral("The ZIP central directory lies outside the file (truncated archive?).");
        return false;
    }

    qint64 at = static_cast<qint64>(directoryOffset);
    for (quint64 i = 0; i < count; ++i) {
        if (le32(data, at) != kCentralHeader) {
            m_warnings.append(QStringLiteral("Central directory ends after %1 of %2 entries.").arg(i).arg(count));
            break;
        }
        ZipEntry e;
        e.flags = le16(data, at + 8);
        e.method = le16(data, at + 10);
        e.crc = le32(data, at + 16);
        e.compressedSize = le32(data, at + 20);
        e.size = le32(data, at + 24);
        const int nameLength = le16(data, at + 28);
        const int extraLength = le16(data, at + 30);
        const int commentLength = le16(data, at + 32);
        e.localOffset = le32(data, at + 42);
        const QByteArray rawName = data.mid(at + 46, nameLength);

        // ZIP64 extra field: the 32-bit values set to $FFFFFFFF follow in order.
        qint64 x = at + 46 + nameLength;
        const qint64 extraEnd = x + extraLength;
        while (x + 4 <= extraEnd) {
            const int id = le16(data, x);
            const int size = le16(data, x + 2);
            if (id == 0x0001) {
                qint64 v = x + 4;
                if (e.size == 0xFFFFFFFF) { e.size = static_cast<qint64>(le64(data, v)); v += 8; }
                if (e.compressedSize == 0xFFFFFFFF) { e.compressedSize = static_cast<qint64>(le64(data, v)); v += 8; }
                if (e.localOffset == 0xFFFFFFFF) { e.localOffset = static_cast<qint64>(le64(data, v)); }
            }
            x += 4 + size;
        }
        at += 46 + nameLength + extraLength + commentLength;

        const QString name = (e.flags & 0x0800) ? QString::fromUtf8(rawName) : cp437(rawName);
        e.isDirectory = name.endsWith(QLatin1Char('/')) || name.endsWith(QLatin1Char('\\'));
        e.path = cleanPath(name);
        if (e.path.isEmpty() || e.path.startsWith(QStringLiteral("__MACOSX")))
            continue;
        m_entries.append(e);
    }
    m_valid = true;
    return true;
}

QByteArray ZipArchive::fileData(const ZipEntry &entry, QString *problem) const
{
    if (entry.flags & 0x0001) {
        *problem = QStringLiteral("encrypted entry (a password would be needed)");
        return {};
    }
    if (entry.method != 0 && entry.method != 8) {
        *problem = QStringLiteral("compression %1 is not supported").arg(methodName(entry.method));
        return {};
    }
    if (entry.size > 0x7FFF0000 || entry.compressedSize > 0x7FFF0000) {
        *problem = QStringLiteral("entry larger than 2 GB");
        return {};
    }
    const qint64 local = entry.localOffset;
    if (le32(m_data, local) != kLocalHeader) {
        *problem = QStringLiteral("local header not found");
        return {};
    }
    const qint64 start = local + 30 + le16(m_data, local + 26) + le16(m_data, local + 28);
    if (start + entry.compressedSize > m_data.size()) {
        *problem = QStringLiteral("data runs past the end of the archive");
        return {};
    }
    const QByteArray stored = m_data.mid(start, entry.compressedSize);
    QByteArray data;
    if (entry.method == 0) {
        data = stored;
    } else if (!inflateRaw(stored, data, entry.size)) {
        *problem = QStringLiteral("damaged deflate data");
        return {};
    }
    if (data.size() != entry.size) {
        *problem = QStringLiteral("size %1 instead of %2").arg(data.size()).arg(entry.size);
        return {};
    }
    if (crc32(data) != entry.crc) {
        *problem = QStringLiteral("CRC-32 mismatch");
        return {};
    }
    return data;
}

QStringList ZipArchive::extractTo(const QString &target)
{
    QStringList written;
    QDir root(target);
    root.mkpath(QStringLiteral("."));
    for (const ZipEntry &e : m_entries) {
        if (e.isDirectory) {
            root.mkpath(e.path);
            continue;
        }
        QString problem;
        const QByteArray data = fileData(e, &problem);
        if (!problem.isEmpty()) {
            m_warnings.append(QStringLiteral("%1: %2").arg(e.path, problem));
            continue;
        }
        const QString path = root.filePath(e.path);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
            m_warnings.append(QStringLiteral("%1: cannot write (%2)").arg(e.path, file.errorString()));
            continue;
        }
        written.append(e.path);
    }
    return written;
}
