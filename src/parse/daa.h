#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

struct DaaBlock
{
    int id = 0;
    quint32 offset = 0;
    quint16 rawSize = 0;
    quint16 compressedSize = 0;
    QByteArray data; // decoded block
};

// Container used by the Amiga Gold Box ports. The header is big-endian:
//   u16 dataOffset
//   { u8 id, u32 offset, u16 rawSize, u16 compressedSize } * ((dataOffset-2)/9)
// Blocks are compressed with the same run-length codec as DOS DAX.
class DaaFile
{
public:
    DaaFile() = default;
    explicit DaaFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    const QVector<DaaBlock> &blocks() const { return m_blocks; }
    quint16 dataOffset() const { return m_dataOffset; }
    QString path() const { return m_path; }

private:
    QString m_path;
    QString m_error;
    quint16 m_dataOffset = 0;
    QVector<DaaBlock> m_blocks;
    bool m_valid = false;
};

// Decodes the image records inside a DAA container. Returns the images and
// fills imageIds with the corresponding DAA block id.
QVector<QImage> decodeDaaImages(const DaaFile &file, QVector<int> *imageIds = nullptr,
                                QString *error = nullptr);

// One decoded graphic record (Load_IndexedGraphicRecord, H029+$0A0E in the
// Champions of Krynn port). The 9-byte big-endian header is
//   u16 rows, u16 count, u16 w4, u16 w6, u8 width8
// and the picture is (width8 * count * 8) pixels wide, rounded up to 16.
// The "8x8..." sets store 4 bitplanes right after the header and use the
// game palette; every other set stores a 32-colour RGB4 palette and 5
// bitplanes. The record is a strip of width8 frames, each count*8 wide.
struct DaaRecordInfo
{
    bool valid = false;
    bool eightByEight = false;
    int rows = 0;
    int count = 0;
    int width8 = 0;
    int width = 0;   // padded picture width in pixels
    int planes = 0;
    QString description;
};

DaaRecordInfo inspectDaaRecord(const QByteArray &data, bool eightByEight);
QVector<QImage> decodeDaaRecord(const QByteArray &data, bool eightByEight,
                                DaaRecordInfo *info = nullptr);

// True when a DAA file name selects the 4-plane 8x8 tile layout.
bool isEightByEightDaaName(const QString &path);

// Picture sequences (pic*.daa, sprit*.daa), read by Load_Sequence
// (H042+$0406). The directory is a big-endian size word followed by 6-byte
// entries (u8 id, u8 unused, u32 offset). Each sequence is
//   u16 headerSize, ByteRun1-packed 0x56-byte header
//     (u16 rows, u16 units, u16 w4, u16 w6, u16 frames,
//      32 RGB4 palette words, 6 u16 packed frame sizes)
//   then, per frame, the ByteRun1-packed 5 bitplanes and a u16 duration.
// Frames after the first are stored XOR-ed with the first frame.
struct DaaSequence
{
    int index = 0;
    int id = 0;
    int flag = 0;
    quint32 offset = 0;     // relative to the end of the directory
    int storedSize = 0;     // bytes used in the file
    int rows = 0;
    int units = 0;
    int w4 = 0;
    int w6 = 0;
    int frameCount = 0;
    QVector<int> packedSizes;
    QVector<int> durations; // tenths of a second
    QVector<QRgb> palette;
    QByteArray header;      // unpacked header (0x56 bytes in CoK, up to 0x5E in DKK)
    QVector<QImage> frames;
    QString error;
};

class DaaSequenceFile
{
public:
    DaaSequenceFile() = default;
    explicit DaaSequenceFile(const QString &path) { load(path); }
    bool load(const QString &path);
    // xorFrames: frames after the first are stored as XOR with it.  Death
    // Knights of Krynn (recognised by its $5E-byte header) draws the "SPRIT"
    // sequences without the XOR (sequence_frame, H049+$0BCE); callers pass
    // false for those files.  Champions of Krynn always XORs.
    bool parse(const QByteArray &bytes, bool xorFrames = true);
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    const QVector<DaaSequence> &sequences() const { return m_sequences; }
    quint16 dataOffset() const { return m_dataOffset; }

private:
    QString m_error;
    quint16 m_dataOffset = 0;
    QVector<DaaSequence> m_sequences;
    bool m_valid = false;
};

// Amiga ILBM ByteRun1 unpacker (ilbm_unpack_row, H088). Returns the number
// of source bytes consumed, or -1 when the source runs out first.
int unpackByteRun1(const QByteArray &source, int sourceOffset, int sourceSize,
                   QByteArray &output, int outputSize);

// Mouse pointers (pointers.daa, load_pointer_data / set_pointer_shape in
// H029): 12 entries of 10 bytes (u32 offset, s16 hot x, s16 hot y,
// u16 height), each pointing at Intuition SetPointer() data (two control
// words, height rows of two 16-bit planes, two end words). The pointer
// colours (sprite colours 17..19) are not in the file.
struct DaaPointer
{
    int index = 0;
    quint32 offset = 0;
    int hotX = 0;
    int hotY = 0;
    int height = 0;
    QImage image;
};

bool decodePointerFile(const QByteArray &data, QVector<DaaPointer> &pointers);
