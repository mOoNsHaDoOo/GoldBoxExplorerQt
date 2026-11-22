#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// ZIP reader (PKWARE APPNOTE) with a built-in deflate decoder, so it needs no
// external library on any system.  Supported: methods 0 (stored) and 8
// (deflate), ZIP64 archives, data descriptors, UTF-8 (flag 11) and code page
// 437 names, CRC-32 check.  Reported and skipped: encrypted entries, other
// methods (Deflate64, bzip2, LZMA, zstd, xz, PPMd ...).  Split archives are
// not supported.  macOS "__MACOSX" metadata entries are ignored.
struct ZipEntry
{
    QString path;           // cleaned, '/' separated, relative
    bool isDirectory = false;
    quint16 method = 0;
    quint16 flags = 0;
    quint32 crc = 0;
    qint64 compressedSize = 0;
    qint64 size = 0;
    qint64 localOffset = 0;
};

class ZipArchive
{
public:
    // A file named *.zip that starts with a ZIP signature.
    static bool looksLikeZip(const QString &path);

    bool load(const QString &path);
    bool parse(const QByteArray &data);

    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    const QVector<ZipEntry> &entries() const { return m_entries; }
    QStringList warnings() const { return m_warnings; }

    // Uncompressed contents; on failure an empty array and *problem is set.
    QByteArray fileData(const ZipEntry &entry, QString *problem) const;

    // Writes every readable entry under target; returns the relative paths
    // of the files written (problems go to warnings()).
    QStringList extractTo(const QString &target);

private:
    QByteArray m_data;
    QVector<ZipEntry> m_entries;
    QStringList m_warnings;
    QString m_error;
    bool m_valid = false;
};

// Raw deflate stream (RFC 1951).  Returns false on a damaged stream.
bool inflateRaw(const QByteArray &input, QByteArray &output, qint64 expectedSize = -1);

quint32 crc32(const QByteArray &data);
