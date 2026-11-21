#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

// One inner block of a multi-block file.
struct ContainerEntry
{
    int index = 0;          // position in the directory
    int id = 0;             // block id used by the game (DAX/DAA), or index
    qint64 offset = -1;     // absolute file offset of the stored bytes, -1 if unknown
    int storedSize = 0;     // bytes in the file (compressed size)
    int declaredSize = 0;   // size declared by the directory (0 = stored)
    QByteArray stored;      // bytes exactly as stored in the file
    QByteArray data;        // decoded block
    bool decoded = true;    // false when the block could not be decompressed
    QString type;           // short detected type
    QString details;        // longer description for the info pane
    QVector<QImage> images; // pre-rendered images for formats without a single-block file
};

// Uniform view of the multi-block files the explorer understands, used by the
// container browser and by `--list`. Parsing follows the existing readers
// (DaxFile, DaaFile, DaaSequenceFile, GlbFile, TlbFile) but keeps the
// directory fields and the stored bytes of every entry.
class Container
{
public:
    enum class Format {
        None,
        DaxPc,        // little-endian 9-byte directory, SSI RLE (DOS, and CoK Amiga .dax)
        DaxAmiga,     // big-endian 10-byte directory, ByteKiller (Amiga Pool of Radiance)
        Daa,          // big-endian 9-byte directory, SSI RLE (Amiga .daa)
        DaaSequence,  // big-endian 6-byte directory, picture sequences (pic*/sprit*.daa)
        Glb,          // GLIB containers (.GLB)
        Tlb,          // GLIB tile libraries (.TLB): one entry per decoded image
    };

    // Returns true and fills the container when path is a multi-block file.
    bool open(const QString &path);

    bool isValid() const { return m_format != Format::None; }
    Format format() const { return m_format; }
    QString formatName() const;
    QString path() const { return m_path; }
    QString summary() const { return m_summary; }
    const QVector<ContainerEntry> &entries() const { return m_entries; }

    // A file in the same format that holds only the given entry, so the
    // normal plugin for this file name can display it. Empty when the format
    // has no such representation (TLB).
    QByteArray singleEntryFile(int index) const;
    bool canExtractSingleEntry() const;

    // Quick check without decoding, used to decide whether to wrap a file.
    static bool looksLikeContainer(const QString &path);

private:
    bool openDaxAmiga(const QByteArray &raw);
    bool openDaxPc(const QByteArray &raw);
    bool openDaa(const QByteArray &raw);
    bool openSequence(const QByteArray &raw);
    bool openGlb(const QByteArray &raw);
    bool openTlb();
    void detectTypes();

    Format m_format = Format::None;
    QString m_path;
    QString m_summary;
    QVector<ContainerEntry> m_entries;
    bool m_glbBigEndian = false;
    QByteArray m_glbHeader;
};
