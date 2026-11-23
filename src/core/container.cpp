#include "core/container.h"

#include "core/daxfile.h"
#include "parse/daa.h"
#include "parse/glb.h"
#include "parse/iff.h"
#include "tbl/tlbfile.h"

#include <QFile>
#include <QFileInfo>

namespace {

quint16 be16(const QByteArray &d, int o)
{
    if (o < 0 || o + 2 > d.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(d.at(o)) << 8) | static_cast<quint8>(d.at(o + 1)));
}

quint32 be32(const QByteArray &d, int o)
{
    return (static_cast<quint32>(be16(d, o)) << 16) | be16(d, o + 2);
}

quint16 le16(const QByteArray &d, int o)
{
    if (o < 0 || o + 2 > d.size())
        return 0;
    return static_cast<quint16>(static_cast<quint8>(d.at(o)) | (static_cast<quint8>(d.at(o + 1)) << 8));
}

quint32 le32(const QByteArray &d, int o)
{
    return static_cast<quint32>(le16(d, o)) | (static_cast<quint32>(le16(d, o + 2)) << 16);
}

void putBe16(QByteArray &d, int v)
{
    d.append(static_cast<char>((v >> 8) & 0xFF));
    d.append(static_cast<char>(v & 0xFF));
}

void putBe32(QByteArray &d, quint32 v)
{
    putBe16(d, static_cast<int>(v >> 16));
    putBe16(d, static_cast<int>(v & 0xFFFF));
}

void putLe16(QByteArray &d, int v)
{
    d.append(static_cast<char>(v & 0xFF));
    d.append(static_cast<char>((v >> 8) & 0xFF));
}

void putLe32(QByteArray &d, quint32 v)
{
    putLe16(d, static_cast<int>(v & 0xFFFF));
    putLe16(d, static_cast<int>(v >> 16));
}

void put32(QByteArray &d, quint32 v, bool bigEndian)
{
    bigEndian ? putBe32(d, v) : putLe32(d, v);
}

void put16(QByteArray &d, int v, bool bigEndian)
{
    bigEndian ? putBe16(d, v) : putLe16(d, v);
}

QString upperName(const QString &path)
{
    return QFileInfo(path).fileName().toUpper();
}

int printableRatioPercent(const QByteArray &data)
{
    if (data.isEmpty())
        return 0;
    int printable = 0;
    for (char c : data) {
        const unsigned char u = static_cast<unsigned char>(c);
        if ((u >= 32 && u < 127) || u == '\n' || u == '\r' || u == '\t' || u == 0)
            ++printable;
    }
    return printable * 100 / data.size();
}

} // namespace

QString Container::formatName() const
{
    switch (m_format) {
    case Format::DaxPc:
        return QStringLiteral("DAX (little-endian 9-byte directory, SSI RLE)");
    case Format::DaxAmiga:
        return QStringLiteral("Amiga DAX (big-endian 10-byte directory, ByteKiller)");
    case Format::Daa:
        return QStringLiteral("Amiga DAA (big-endian 9-byte directory, SSI RLE)");
    case Format::DaaSequence:
        return QStringLiteral("Amiga DAA picture sequences (6-byte directory, ByteRun1)");
    case Format::Glb:
        return QStringLiteral("GLIB container");
    case Format::Tlb:
        return QStringLiteral("GLIB tile library (one entry per image)");
    case Format::None:
        break;
    }
    return QStringLiteral("not a container");
}

bool Container::looksLikeContainer(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toUpper();
    return suffix == QStringLiteral("DAX") || suffix == QStringLiteral("DAA")
           || suffix == QStringLiteral("DAI") || suffix == QStringLiteral("GLB")
           || suffix == QStringLiteral("TLB");
}

bool Container::open(const QString &path)
{
    m_format = Format::None;
    m_path = path;
    m_entries.clear();
    m_summary.clear();

    if (!looksLikeContainer(path))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    if (raw.size() < 8)
        return false;

    const QString suffix = QFileInfo(path).suffix().toUpper();
    bool ok = false;
    if (suffix == QStringLiteral("GLB")) {
        ok = openGlb(raw);
    } else if (suffix == QStringLiteral("TLB")) {
        ok = openTlb();
    } else if (suffix == QStringLiteral("DAX")) {
        ok = openDaxAmiga(raw) || openDaxPc(raw);
    } else {
        ok = openDaa(raw) || openSequence(raw) || openDaxAmiga(raw) || openDaxPc(raw);
    }
    if (!ok || m_entries.isEmpty()) {
        m_format = Format::None;
        m_entries.clear();
        return false;
    }
    detectTypes();
    return true;
}

bool Container::openDaxAmiga(const QByteArray &raw)
{
    const int indexSize = be16(raw, 0);
    if (indexSize < 10 || indexSize % 10 != 0 || 2 + indexSize > raw.size())
        return false;
    const int count = indexSize / 10;
    const int base = 2 + indexSize;
    QVector<ContainerEntry> entries;
    for (int i = 0; i < count; ++i) {
        const int at = 2 + i * 10;
        ContainerEntry e;
        e.index = i;
        e.id = be16(raw, at);
        const quint32 offset = be32(raw, at + 2);
        e.storedSize = be16(raw, at + 6);
        e.declaredSize = be16(raw, at + 8);
        if (e.storedSize <= 0 || e.declaredSize <= 0
            || offset + static_cast<quint32>(e.storedSize) > static_cast<quint32>(raw.size() - base))
            return false;
        e.offset = base + offset;
        e.stored = raw.mid(static_cast<int>(e.offset), e.storedSize);
        e.decoded = DaxFile::unpackByteKiller(e.stored, e.declaredSize, e.data);
        // Same acceptance rule as DaxFile: the first block must unpack.
        if (i == 0 && !e.decoded)
            return false;
        entries.append(e);
    }
    m_format = Format::DaxAmiga;
    m_entries = entries;
    m_summary = QStringLiteral("Directory: %1 bytes, %2 entries").arg(indexSize).arg(count);
    return true;
}

bool Container::openDaxPc(const QByteArray &raw)
{
    const int dataOffset = static_cast<qint16>(le16(raw, 0)) + 2;
    if (dataOffset < 11 || dataOffset > raw.size() || (dataOffset - 2) % 9 != 0)
        return false;
    const int count = (dataOffset - 2) / 9;
    QVector<ContainerEntry> entries;
    for (int i = 0; i < count; ++i) {
        const int at = 2 + i * 9;
        ContainerEntry e;
        e.index = i;
        e.id = static_cast<quint8>(raw.at(at));
        const qint32 offset = static_cast<qint32>(le32(raw, at + 1));
        e.declaredSize = le16(raw, at + 5);
        e.storedSize = le16(raw, at + 7);
        if (offset < 0 || e.storedSize <= 0
            || static_cast<qint64>(dataOffset) + offset + e.storedSize > raw.size())
            return false;
        e.offset = dataOffset + offset;
        e.stored = raw.mid(static_cast<int>(e.offset), e.storedSize);
        if (e.declaredSize <= 0) {
            e.data = e.stored; // stored uncompressed
        } else {
            e.data = DaxFile::decode(e.stored, e.declaredSize);
        }
        entries.append(e);
    }
    m_format = Format::DaxPc;
    m_entries = entries;
    m_summary = QStringLiteral("Directory: %1 bytes, %2 entries").arg(dataOffset).arg(count);
    return true;
}

bool Container::openDaa(const QByteArray &raw)
{
    DaaFile daa(m_path);
    if (!daa.isValid())
        return false;
    QVector<ContainerEntry> entries;
    int index = 0;
    for (const DaaBlock &block : daa.blocks()) {
        ContainerEntry e;
        e.index = index++;
        e.id = block.id;
        e.offset = daa.dataOffset() + static_cast<qint64>(block.offset);
        e.storedSize = block.compressedSize;
        e.declaredSize = block.rawSize;
        e.stored = raw.mid(static_cast<int>(e.offset), e.storedSize);
        e.data = block.data;
        entries.append(e);
    }
    m_format = Format::Daa;
    m_entries = entries;
    m_summary = QStringLiteral("Directory: %1 bytes, %2 entries").arg(daa.dataOffset()).arg(entries.size());
    return true;
}

bool Container::openSequence(const QByteArray &raw)
{
    DaaSequenceFile file;
    if (!file.parse(raw, !upperName(m_path).startsWith(QStringLiteral("SPRIT"))))
        return false;
    QVector<ContainerEntry> entries;
    for (const DaaSequence &seq : file.sequences()) {
        ContainerEntry e;
        e.index = seq.index;
        e.id = seq.id;
        e.offset = file.dataOffset() + static_cast<qint64>(seq.offset);
        e.storedSize = seq.storedSize;
        e.declaredSize = 0;
        e.stored = raw.mid(static_cast<int>(e.offset), e.storedSize);
        e.data = e.stored; // frames are packed one by one; see the View tab
        e.decoded = seq.error.isEmpty();
        e.images = seq.frames;
        QStringList durations;
        for (int d : seq.durations)
            durations.append(QString::number(d));
        e.type = QStringLiteral("Sequence %1x%2, %3 frame(s)")
                     .arg(seq.units * 8).arg(seq.rows).arg(seq.frameCount);
        e.details = QStringLiteral("Picture sequence %1 (directory flag %2)\n"
                                   "Size: %3x%4 (%5 units), w4=%6 w6=%7\n"
                                   "Frames: %8, packed sizes: %9\n"
                                   "Durations (1/10 s): %10\n%11")
                        .arg(seq.id).arg(seq.flag)
                        .arg(seq.units * 8).arg(seq.rows).arg(seq.units)
                        .arg(seq.w4).arg(seq.w6)
                        .arg(seq.frameCount)
                        .arg([&] {
                            QStringList s;
                            for (int v : seq.packedSizes)
                                s.append(QString::number(v));
                            return s.join(QLatin1Char(' '));
                        }())
                        .arg(durations.join(QLatin1Char(' ')))
                        .arg(seq.error.isEmpty() ? QString()
                                                 : QStringLiteral("Error: %1\n").arg(seq.error));
        entries.append(e);
    }
    m_format = Format::DaaSequence;
    m_entries = entries;
    m_summary = QStringLiteral("Directory: %1 bytes, %2 sequences").arg(file.dataOffset()).arg(entries.size());
    return true;
}

bool Container::openGlb(const QByteArray &raw)
{
    GlbFile glb(m_path);
    if (!glb.isValid())
        return false;
    m_glbBigEndian = glb.isBigEndian();
    m_glbHeader = raw.left(16);
    QVector<ContainerEntry> entries;
    for (const GlbBlock &block : glb.blocks()) {
        ContainerEntry e;
        e.index = block.index;
        e.id = block.index;
        e.offset = block.offset;
        e.storedSize = block.data.size();
        e.stored = block.data;
        e.data = block.data;
        entries.append(e);
    }
    m_format = Format::Glb;
    m_entries = entries;
    m_summary = QStringLiteral("%1-endian GLIB, content type %2, %3 blocks")
                    .arg(glb.isBigEndian() ? QStringLiteral("Big") : QStringLiteral("Little"))
                    .arg(glb.contentType())
                    .arg(entries.size());
    return true;
}

bool Container::openTlb()
{
    TlbFile tlb(m_path);
    if (!tlb.isValid())
        return false;
    tlb.loadBitmaps();
    const QVector<QImage> &bitmaps = tlb.bitmaps();
    if (bitmaps.isEmpty())
        return false;
    QVector<ContainerEntry> entries;
    for (int i = 0; i < bitmaps.size(); ++i) {
        ContainerEntry e;
        e.index = i;
        e.id = i;
        e.images = {bitmaps.at(i)};
        e.type = QStringLiteral("%1 %2x%3")
                     .arg(tlb.imageIsMaskOnly(i) ? QStringLiteral("Mask sprite") : QStringLiteral("Image"))
                     .arg(bitmaps.at(i).width()).arg(bitmaps.at(i).height());
        e.details = QStringLiteral("Decoded TLB image %1 (the stored bytes are not exposed by the TLB reader).")
                        .arg(i);
        if (tlb.imageIsMaskOnly(i))
            e.details += QStringLiteral("\nMask sprite: the bitplanes are empty, only the mask has a shape (shown in"
                                        " light grey); the game probably fills it with a colour when it draws it.");
        entries.append(e);
    }
    m_format = Format::Tlb;
    m_entries = entries;
    m_summary = QStringLiteral("%1 decoded images").arg(entries.size());
    return true;
}

void Container::detectTypes()
{
    const QString name = upperName(m_path);
    const bool eightByEight = name.startsWith(QStringLiteral("8X8"));
    const QString glbContent = m_format == Format::Glb ? GlbFile(m_path).contentType() : QString();

    for (ContainerEntry &e : m_entries) {
        if (!e.type.isEmpty())
            continue;
        if (!e.decoded) {
            e.type = QStringLiteral("Undecodable block");
            e.details = QStringLiteral("The stored bytes did not decompress.");
            continue;
        }
        const int size = e.data.size();

        if (m_format == Format::Daa) {
            const DaaRecordInfo info = inspectDaaRecord(e.data, eightByEight);
            e.type = info.valid ? (eightByEight ? QStringLiteral("8x8 tiles (%1)").arg(info.width8)
                                  : info.width8 > 1
                                      ? QStringLiteral("Image strip %1x%2 x%3").arg(info.count * 8).arg(info.rows).arg(info.width8)
                                      : QStringLiteral("Image %1x%2").arg(info.count * 8).arg(info.rows))
                                : (info.description.startsWith(QStringLiteral("Empty"))
                                       ? QStringLiteral("Empty block")
                                       : QStringLiteral("Unknown data"));
            e.details = QStringLiteral("Graphic record (Load_IndexedGraphicRecord)\n%1\n"
                                       "Header: rows %2, count %3, width8 %4")
                            .arg(info.description).arg(info.rows).arg(info.count).arg(info.width8);
            continue;
        }

        if (m_format == Format::Glb) {
            const QString &content = glbContent;
            if (IffForm form; readIffForm(e.data, form))
                e.type = QStringLiteral("IFF %1").arg(QString::fromLatin1(form.type));
            else if (content == QStringLiteral("TEXT") || printableRatioPercent(e.data) > 90)
                e.type = QStringLiteral("Text");
            else if (name.startsWith(QStringLiteral("ECL")))
                e.type = QStringLiteral("ECL script");
            else
                e.type = QStringLiteral("%1 block").arg(content);
            e.details = QStringLiteral("GLIB block %1 (%2 bytes)").arg(e.index).arg(size);
            continue;
        }

        // DAX: the game selects the block layout by file name.
        if (name.startsWith(QStringLiteral("ECL"))) {
            e.type = QStringLiteral("ECL script");
        } else if (name.startsWith(QStringLiteral("GEO"))) {
            e.type = size == 1026 || size == 0x402 ? QStringLiteral("GEO map 16x16")
                                                   : QStringLiteral("GEO map");
        } else if (name.startsWith(QStringLiteral("WALLDEF"))) {
            e.type = QStringLiteral("Wall definitions (%1 set(s))").arg(size / 780);
        } else if (name.startsWith(QStringLiteral("8X8"))) {
            e.type = QStringLiteral("8x8 tiles");
        } else if (name.contains(QStringLiteral("ITM")) || name.contains(QStringLiteral("ITEM"))) {
            e.type = size % 63 == 0 ? QStringLiteral("Item records (%1 x 63)").arg(size / 63)
                                    : QStringLiteral("Item data");
        } else if (name.contains(QStringLiteral("SPC"))) {
            e.type = size % 9 == 0 ? QStringLiteral("Effect records (%1 x 9)").arg(size / 9)
                                   : QStringLiteral("Effect data");
        } else if (name.contains(QStringLiteral("CHA"))) {
            e.type = QStringLiteral("Monster record");
        } else if (name.contains(QStringLiteral("WIZ"))) {
            e.type = QStringLiteral("Monster spell data");
        } else {
            e.type = QStringLiteral("Image");
        }
        e.details = QStringLiteral("%1 block %2: %3 bytes decoded from %4 stored bytes")
                        .arg(m_format == Format::DaxAmiga ? QStringLiteral("Amiga DAX")
                                                          : QStringLiteral("DAX"))
                        .arg(e.id).arg(size).arg(e.storedSize);
    }
}

bool Container::canExtractSingleEntry() const
{
    return m_format != Format::None && m_format != Format::Tlb;
}

QByteArray Container::singleEntryFile(int index) const
{
    if (index < 0 || index >= m_entries.size() || !canExtractSingleEntry())
        return {};
    const ContainerEntry &e = m_entries.at(index);
    QByteArray out;
    switch (m_format) {
    case Format::DaxPc:
        putLe16(out, 9);
        out.append(static_cast<char>(e.id & 0xFF));
        putLe32(out, 0);
        putLe16(out, e.declaredSize);
        putLe16(out, e.storedSize);
        out += e.stored;
        break;
    case Format::DaxAmiga:
        putBe16(out, 10);
        putBe16(out, e.id);
        putBe32(out, 0);
        putBe16(out, e.storedSize);
        putBe16(out, e.declaredSize);
        out += e.stored;
        break;
    case Format::Daa:
        putBe16(out, 11);
        out.append(static_cast<char>(e.id & 0xFF));
        putBe32(out, 0);
        putBe16(out, e.declaredSize);
        putBe16(out, e.storedSize);
        out += e.stored;
        break;
    case Format::DaaSequence:
        putBe16(out, 8);
        out.append(static_cast<char>(e.id & 0xFF));
        out.append('\0');
        putBe32(out, 0);
        out += e.stored;
        break;
    case Format::Glb: {
        // No id table: u32 first-data offset at 16, then one block offset.
        out = m_glbHeader.left(4);
        const quint32 total = 24 + static_cast<quint32>(e.stored.size());
        put32(out, total, m_glbBigEndian);
        put16(out, 2, m_glbBigEndian);
        out.append('\0');
        out.append('\0');
        out += m_glbHeader.mid(12, 4);
        put32(out, 24, m_glbBigEndian);
        put32(out, 24, m_glbBigEndian);
        out += e.stored;
        break;
    }
    case Format::Tlb:
    case Format::None:
        break;
    }
    return out;
}
