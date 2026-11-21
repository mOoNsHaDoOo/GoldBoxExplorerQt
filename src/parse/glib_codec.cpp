#include "parse/glib_codec.h"

#include <QVector>

namespace {

constexpr int kHeaderSize = 16;
constexpr int kSlice = 0x2000;

quint32 be32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

// Bounds-checked input stream; any read past the end marks it as broken.
class Input
{
public:
    Input(const QByteArray &data, int pos) : m_data(data), m_pos(pos) {}
    quint8 next()
    {
        if (m_pos >= m_data.size()) {
            m_ok = false;
            return 0;
        }
        return static_cast<quint8>(m_data.at(m_pos++));
    }
    bool ok() const { return m_ok; }

private:
    const QByteArray &m_data;
    int m_pos;
    bool m_ok = true;
};

// Output written at an explicit position: each slice starts at a fixed
// offset, whatever the previous slice produced beyond it.
class Output
{
public:
    explicit Output(int capacity) { m_data.reserve(capacity + kSlice); }
    void startAt(int pos) { m_data.truncate(pos); }
    int size() const { return m_data.size(); }
    void put(quint8 value) { m_data.append(static_cast<char>(value)); }
    bool copyBack(int distance, int count)
    {
        int from = m_data.size() - distance;
        if (from < 0)
            return false;
        for (int i = 0; i < count; ++i)
            m_data.append(m_data.at(from + i));
        return true;
    }
    QByteArray &data() { return m_data; }

private:
    QByteArray m_data;
};

// Method 5 ($454f2): decodes until the end token, returns false on damage.
bool decodeLz(Input &in, Output &out)
{
    for (int guard = 0; guard < 0x100000; ++guard) {
        const quint8 c = in.next();
        if (!in.ok())
            return false;
        if (!(c & 0x80)) {
            const quint8 b = in.next();
            for (int i = 0; i < ((b >> 3) & 3); ++i)
                out.put(in.next());
            const int distance = (((b << 2) & 0x380) | c) + 1;
            if (!out.copyBack(distance, (b & 7) + 3))
                return false;
        } else if (!(c & 0x40)) {
            const quint8 b = in.next();
            const quint8 d = in.next();
            for (int i = 0; i < (d & 3); ++i)
                out.put(in.next());
            const int distance = (((b & 0xE0) << 1) | (c & 0x3F) | ((d << 7) & 0x7E00)) + 1;
            if (!out.copyBack(distance, (b & 0x1F) + 3))
                return false;
        } else if (!(c & 0x20)) {
            int count = c & 0x0F;
            if (!(c & 0x10))
                count = (count + 1) * 4;
            for (int i = 0; i < count; ++i)
                out.put(in.next());
        } else {
            return in.ok();
        }
        if (!in.ok())
            return false;
    }
    return false;
}

// Method 3: 10-bit LZW.  flag[] is the game's state byte per code: 0 free,
// 0xFF leaf, 1 used as a prefix.  Codes 0..255 are the literal bytes.
class Lzw
{
public:
    Lzw()
        : m_flag(0x402, 0), m_suffix(0x400, 0), m_prefix(0x400, 0)
    {
        for (int i = 0; i < 0x100; ++i) {
            m_flag[i] = 0xFF;
            m_suffix[i] = static_cast<quint8>(i);
            m_prefix[i] = 0xFFFF;
        }
    }

    bool slice(Input &in, Output &out, int end)
    {
        const quint8 hi = in.next();
        const quint8 lo = in.next();
        quint16 bits = static_cast<quint16>((hi << 8) | lo);
        int previous = bits >> 6;
        int remaining = 6;
        if (!emitCode(previous, out))
            return false;
        int next = m_next;
        while (out.size() < end) {
            if (remaining) {
                bits = static_cast<quint16>((bits << 8) | in.next());
                remaining -= 2;
            } else {
                const quint8 a = in.next();
                const quint8 b = in.next();
                bits = static_cast<quint16>((a << 8) | b);
                remaining = 6;
            }
            if (!in.ok())
                return false;
            const int code = (bits >> remaining) & 0x3FF;
            int first = 0;
            if (!emitCode(code, out, &first))
                return false;
            if (next >= 0x400)
                next = recycle(previous, next);
            m_flag[previous] = 1;
            m_flag[next] = 0xFF;
            m_suffix[next] = static_cast<quint8>(first);
            m_prefix[next] = static_cast<quint16>(previous);
            ++next;
            while (next < 0x400 && m_flag[next])
                ++next;
            previous = code;
        }
        m_next = next;
        return true;
    }

private:
    bool emitCode(int code, Output &out, int *first = nullptr)
    {
        QVector<quint8> stack;
        int guard = 0;
        while (code >= 0x100) {
            if (++guard > 0x400)
                return false;
            stack.append(m_suffix[code]);
            code = m_prefix[code];
            if (code >= 0x400)
                return false;
        }
        out.put(static_cast<quint8>(code));
        for (int i = stack.size() - 1; i >= 0; --i)
            out.put(stack.at(i));
        if (first)
            *first = code;
        return true;
    }

    // $41abe: frees the leaves (except the last code) until at least $55
    // codes are free; returns the lowest free code.
    int recycle(int previous, int next)
    {
        int freed = 0;
        for (int pass = 0; pass < 64; ++pass) {
            for (int i = 0x100; i < 0x400; ++i) {
                const quint8 f = m_flag[i];
                if (f == 0xFF && i != previous) {
                    m_flag[i] = 0;
                    ++freed;
                    if (i < next)
                        next = i;
                } else if (f > 0 && f < 0x80) {
                    m_flag[i] = 0xFF;
                }
            }
            for (int i = 0x100; i < 0x400; ++i) {
                if (m_flag[i] && m_prefix[i] < 0x400)
                    m_flag[m_prefix[i]] = 1;
            }
            if (freed >= 0x55)
                break;
        }
        return next;
    }

    QVector<quint8> m_flag;
    QVector<quint8> m_suffix;
    QVector<quint16> m_prefix;
    int m_next = 0x100;
};

} // namespace

namespace GlibCodec {

int compressionMethod(const QByteArray &library)
{
    if (library.size() < kHeaderSize || library.left(4) != QByteArrayLiteral("GLIB"))
        return -1;
    return static_cast<quint8>(library.at(10));
}

QByteArray decompress(const QByteArray &library, QString *error)
{
    const int method = compressionMethod(library);
    if (method <= 0)
        return library;

    const quint32 size = be32(library, 4);
    if (size < kHeaderSize + 2 || size > 0x1000000) {
        if (error)
            *error = QStringLiteral("GLIB library has an invalid size (%1).").arg(size);
        return {};
    }
    if (method != 3 && method != 5) {
        if (error)
            *error = QStringLiteral("GLIB compression method %1 is not supported.").arg(method);
        return {};
    }

    const int total = static_cast<int>(size) - kHeaderSize;
    Input in(library, kHeaderSize + 2);
    Output out(total);
    Lzw lzw;
    for (int pos = 0; pos < total; pos += kSlice) {
        const int end = qMin(total, pos + kSlice);
        out.startAt(pos);
        const bool ok = method == 5 ? decodeLz(in, out) : lzw.slice(in, out, end);
        if (!ok || out.size() < end) {
            if (error)
                *error = QStringLiteral("GLIB method %1 stream is damaged at output byte %2.")
                             .arg(method)
                             .arg(out.size());
            return {};
        }
    }
    out.data().truncate(total);

    QByteArray result = library.left(kHeaderSize);
    result[10] = 0;
    result.append(out.data());
    return result;
}

} // namespace GlibCodec
