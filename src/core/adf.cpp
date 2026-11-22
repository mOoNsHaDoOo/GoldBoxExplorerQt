#include "core/adf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {

constexpr int kBlockSize = 512;
constexpr int kHashSize = 72;          // entries of a hash table (512-byte blocks)
constexpr qint32 kTypeHeader = 2;
constexpr qint32 kTypeList = 16;
constexpr qint32 kTypeData = 8;
constexpr qint32 kSecRoot = 1;
constexpr qint32 kSecDir = 2;
constexpr qint32 kSecFile = -3;

// Characters that cannot appear in a host file name.
QString safeName(const QString &name)
{
    QString out = name;
    for (QChar &c : out) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
            || c.unicode() < 32)
            c = QLatin1Char('_');
    }
    if (out.isEmpty() || out == QStringLiteral(".") || out == QStringLiteral(".."))
        out = QStringLiteral("_") + out;
    return out;
}

} // namespace

bool AdfImage::looksLikeAdf(const QString &path)
{
    const QFileInfo info(path);
    return info.suffix().compare(QStringLiteral("adf"), Qt::CaseInsensitive) == 0
           && info.size() >= 2 * kBlockSize && info.size() % kBlockSize == 0;
}

bool AdfImage::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        m_valid = false;
        return false;
    }
    return parse(file.readAll());
}

QByteArray AdfImage::block(int n) const
{
    if (n < 0 || n >= m_blocks)
        return {};
    return m_image.mid(n * kBlockSize, kBlockSize);
}

quint32 AdfImage::longAt(const QByteArray &b, int offset) const
{
    if (offset < 0 || offset + 4 > b.size())
        return 0;
    return (static_cast<quint32>(static_cast<quint8>(b.at(offset))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(b.at(offset + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(b.at(offset + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(b.at(offset + 3)));
}

QString AdfImage::nameOf(const QByteArray &b) const
{
    const int length = qMin(30, static_cast<int>(static_cast<quint8>(b.at(kBlockSize - 80))));
    return QString::fromLatin1(b.mid(kBlockSize - 79, length));
}

// days since 1 Jan 1978, minutes, ticks (1/50 s) at block size - 92.
QDateTime AdfImage::dateOf(const QByteArray &b) const
{
    const qint64 days = longAt(b, kBlockSize - 92);
    const qint64 minutes = longAt(b, kBlockSize - 88);
    const qint64 ticks = longAt(b, kBlockSize - 84);
    if (days > 100000 || minutes >= 24 * 60)
        return {};
    QDateTime t(QDate(1978, 1, 1), QTime(0, 0), Qt::UTC);
    return t.addDays(days).addSecs(minutes * 60 + ticks / 50);
}

bool AdfImage::parse(const QByteArray &image)
{
    m_image = image;
    m_nodes.clear();
    m_entries.clear();
    m_warnings.clear();
    m_error.clear();
    m_volume.clear();
    m_valid = false;
    m_blocks = image.size() / kBlockSize;

    if (image.size() < 4 * kBlockSize || image.size() % kBlockSize != 0) {
        m_error = QStringLiteral("Not an Amiga disk image (size %1).").arg(image.size());
        return false;
    }
    if (image.left(3) != QByteArrayLiteral("DOS")) {
        m_error = QStringLiteral("Not an AmigaDOS disk: the boot block starts with %1 instead of DOS. "
                                 "Disks with a custom format (many games) cannot be read as files.")
                      .arg(QString::fromLatin1(image.left(4).toHex(' ')));
        return false;
    }
    const quint8 flags = static_cast<quint8>(image.at(3));
    if (flags > 7) {
        m_error = QStringLiteral("Unknown AmigaDOS flavour DOS\\%1.").arg(flags);
        return false;
    }
    m_ffs = flags & 1;

    int root = static_cast<int>(longAt(image.left(kBlockSize), 8));
    if (root <= 1 || root >= m_blocks)
        root = m_blocks / 2;
    const QByteArray rootBlock = block(root);
    if (static_cast<qint32>(longAt(rootBlock, 0)) != kTypeHeader
        || static_cast<qint32>(longAt(rootBlock, kBlockSize - 4)) != kSecRoot) {
        m_error = QStringLiteral("The root block %1 is not valid.").arg(root);
        return false;
    }
    m_volume = nameOf(rootBlock);
    walk(root, QString(), 0);
    for (const Node &node : m_nodes)
        m_entries.append(node.entry);
    m_valid = true;
    return true;
}

void AdfImage::walk(int directoryBlock, const QString &prefix, int depth)
{
    if (depth > 32)
        return;
    const QByteArray dir = block(directoryBlock);
    const int tableSize = qMin(kHashSize, static_cast<int>(longAt(dir, 12)) > 0
                                              ? static_cast<int>(longAt(dir, 12)) : kHashSize);
    QSet<int> seen;
    QVector<Node> found;
    for (int i = 0; i < tableSize; ++i) {
        int next = static_cast<int>(longAt(dir, 24 + i * 4));
        while (next > 0 && next < m_blocks && !seen.contains(next)) {
            seen.insert(next);
            const QByteArray header = block(next);
            const qint32 type = static_cast<qint32>(longAt(header, 0));
            const qint32 secondary = static_cast<qint32>(longAt(header, kBlockSize - 4));
            if (type != kTypeHeader) {
                m_warnings.append(QStringLiteral("Block %1 in %2 is not a header block.")
                                      .arg(next).arg(prefix.isEmpty() ? QStringLiteral("/") : prefix));
                break;
            }
            Node node;
            node.block = next;
            node.entry.path = prefix + safeName(nameOf(header));
            node.entry.modified = dateOf(header);
            const int commentLength = qMin(79, static_cast<int>(static_cast<quint8>(header.at(kBlockSize - 184))));
            node.entry.comment = QString::fromLatin1(header.mid(kBlockSize - 183, commentLength));
            if (secondary == kSecDir) {
                node.entry.isDirectory = true;
            } else if (secondary == kSecFile) {
                node.entry.size = longAt(header, kBlockSize - 188);
            } else {
                // links and other entry types are listed but not extracted
                m_warnings.append(QStringLiteral("%1: entry type %2 skipped.").arg(node.entry.path).arg(secondary));
                next = static_cast<int>(longAt(header, kBlockSize - 16));
                continue;
            }
            found.append(node);
            next = static_cast<int>(longAt(header, kBlockSize - 16)); // hash chain
        }
    }
    std::sort(found.begin(), found.end(), [](const Node &a, const Node &b) {
        return a.entry.path.compare(b.entry.path, Qt::CaseInsensitive) < 0;
    });
    for (const Node &node : found) {
        m_nodes.append(node);
        if (node.entry.isDirectory)
            walk(node.block, node.entry.path + QLatin1Char('/'), depth + 1);
    }
}

QByteArray AdfImage::readFile(int headerBlock, QString *problem) const
{
    QByteArray header = block(headerBlock);
    const qint64 size = longAt(header, kBlockSize - 188);
    if (size > m_image.size()) {
        *problem = QStringLiteral("size %1 larger than the disk").arg(size);
        return {};
    }
    QVector<int> dataBlocks;
    QSet<int> visited;
    int current = headerBlock;
    while (current > 0 && !visited.contains(current)) {
        visited.insert(current);
        const int count = qMin(kHashSize, static_cast<int>(longAt(header, 8)));
        for (int i = 0; i < count; ++i)
            dataBlocks.append(static_cast<int>(longAt(header, 24 + (kHashSize - 1 - i) * 4)));
        current = static_cast<int>(longAt(header, kBlockSize - 8)); // extension block
        if (current <= 0 || current >= m_blocks)
            break;
        header = block(current);
        if (static_cast<qint32>(longAt(header, 0)) != kTypeList) {
            *problem = QStringLiteral("bad extension block %1").arg(current);
            break;
        }
    }

    QByteArray data;
    data.reserve(static_cast<int>(size));
    for (int n : dataBlocks) {
        if (data.size() >= size)
            break;
        if (n <= 0 || n >= m_blocks) {
            *problem = QStringLiteral("data block %1 outside the disk").arg(n);
            break;
        }
        const QByteArray b = block(n);
        if (m_ffs) {
            data.append(b);
        } else {
            if (static_cast<qint32>(longAt(b, 0)) != kTypeData) {
                *problem = QStringLiteral("block %1 is not an OFS data block").arg(n);
                break;
            }
            const int used = qMin(kBlockSize - 24, static_cast<int>(longAt(b, 12)));
            data.append(b.mid(24, used));
        }
    }
    if (data.size() < size && problem->isEmpty())
        *problem = QStringLiteral("%1 of %2 bytes found").arg(data.size()).arg(size);
    data.truncate(static_cast<int>(size));
    return data;
}

QByteArray AdfImage::fileData(const QString &path) const
{
    for (const Node &node : m_nodes) {
        if (!node.entry.isDirectory && node.entry.path == path) {
            QString problem;
            return readFile(node.block, &problem);
        }
    }
    return {};
}

bool AdfImage::extractTo(const QString &target)
{
    QDir root(target);
    if (!root.mkpath(QStringLiteral("."))) {
        m_error = QStringLiteral("Cannot create %1.").arg(target);
        return false;
    }
    for (const Node &node : m_nodes) {
        const QString path = root.filePath(node.entry.path);
        if (node.entry.isDirectory) {
            root.mkpath(node.entry.path);
            continue;
        }
        QString problem;
        const QByteArray data = readFile(node.block, &problem);
        if (!problem.isEmpty())
            m_warnings.append(QStringLiteral("%1: %2").arg(node.entry.path, problem));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
            m_error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
            return false;
        }
        file.flush();
        // setFileTime needs the file open; the date of the disk entry is kept.
        if (node.entry.modified.isValid())
            file.setFileTime(node.entry.modified, QFileDevice::FileModificationTime);
        file.close();
    }
    return true;
}
