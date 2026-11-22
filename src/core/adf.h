#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// Amiga disk images (.adf): AmigaDOS OFS and FFS file systems, double and
// high density (880 KB and 1760 KB).  The layout is the documented AmigaDOS
// one: boot block, root block in the middle of the disk, 72-entry hash
// tables, file header blocks with their data block lists and extensions.
struct AdfEntry
{
    QString path;       // relative, '/' separated
    bool isDirectory = false;
    qint64 size = 0;
    QDateTime modified;
    QString comment;
};

class AdfImage
{
public:
    // A file named *.adf whose size is a whole number of 512-byte blocks.
    static bool looksLikeAdf(const QString &path);

    bool load(const QString &path);
    bool parse(const QByteArray &image);

    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    QString volumeName() const { return m_volume; }
    QString fileSystem() const { return m_ffs ? QStringLiteral("FFS") : QStringLiteral("OFS"); }
    int blockCount() const { return m_blocks; }
    const QVector<AdfEntry> &entries() const { return m_entries; }
    QStringList warnings() const { return m_warnings; }

    // Contents of one file (as listed in entries()).
    QByteArray fileData(const QString &path) const;

    // Writes every directory and file under target (created if needed);
    // returns false and sets errorString() when a file cannot be written.
    bool extractTo(const QString &target);

private:
    struct Node
    {
        int block = 0;
        AdfEntry entry;
    };

    QByteArray block(int n) const;
    quint32 longAt(const QByteArray &b, int offset) const;
    QString nameOf(const QByteArray &b) const;
    QDateTime dateOf(const QByteArray &b) const;
    void walk(int directoryBlock, const QString &prefix, int depth);
    QByteArray readFile(int headerBlock, QString *problem) const;

    QByteArray m_image;
    QVector<Node> m_nodes;
    QVector<AdfEntry> m_entries;
    QStringList m_warnings;
    QString m_error;
    QString m_volume;
    int m_blocks = 0;
    bool m_ffs = false;
    bool m_valid = false;
};
