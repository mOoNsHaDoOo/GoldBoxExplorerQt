#include "parse/audio.h"

#include "core/byteutils.h"
#include "parse/iff.h"

#include <QtEndian>

namespace {

quint16 be16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}


QByteArray findChunk(const IffForm &form, const QByteArray &id)
{
    for (const IffChunk &chunk : form.chunks) {
        if (chunk.id == id)
            return chunk.data;
    }
    return {};
}

} // namespace

bool looksLike8svx(const QByteArray &data)
{
    return looksLikeIff(data, QByteArrayLiteral("8SVX"))
           || looksLikeIff(data, QByteArrayLiteral("16SV"));
}

bool load8svx(const QByteArray &data, AudioSample &sample, QString *error)
{
    sample = {};
    IffForm form;
    if (!readIffForm(data, form, error))
        return false;

    if (form.type != QByteArrayLiteral("8SVX") && form.type != QByteArrayLiteral("16SV")) {
        if (error)
            *error = QStringLiteral("Not an IFF 8SVX/16SV file.");
        return false;
    }

    const QByteArray vhdr = findChunk(form, QByteArrayLiteral("VHDR"));
    const QByteArray body = findChunk(form, QByteArrayLiteral("BODY"));
    if (vhdr.size() < 20 || body.isEmpty()) {
        if (error)
            *error = QStringLiteral("Missing VHDR or BODY chunk.");
        return false;
    }

    sample.sampleRate = be16(vhdr, 12);
    if (sample.sampleRate <= 0)
        sample.sampleRate = 8000;

    const int compression = byteAt(vhdr, 15);
    if (compression != 0) {
        if (error)
            *error = QStringLiteral("Compressed 8SVX audio (type %1) is not supported.")
                         .arg(compression);
        return false;
    }

    sample.name = QString::fromLatin1(findChunk(form, QByteArrayLiteral("NAME")));
    sample.bits = form.type == QByteArrayLiteral("16SV") ? 16 : 8;
    sample.channels = 1;

    if (sample.bits == 8) {
        sample.frameCount = body.size();
        sample.pcm.resize(body.size() * 2);
        for (int i = 0; i < body.size(); ++i) {
            const qint8 value = static_cast<qint8>(body.at(i));
            const qint16 widened = static_cast<qint16>(value) * 256;
            qToLittleEndian<qint16>(widened, sample.pcm.data() + i * 2);
        }
    } else {
        sample.frameCount = body.size() / 2;
        sample.pcm.resize(sample.frameCount * 2);
        for (int i = 0; i < sample.frameCount; ++i) {
            const qint16 value = static_cast<qint16>(be16(body, i * 2));
            qToLittleEndian<qint16>(value, sample.pcm.data() + i * 2);
        }
    }

    sample.valid = true;
    return true;
}

QByteArray AudioSample::toWav() const
{
    const int dataSize = pcm.size();
    const int sampleRateValue = sampleRate > 0 ? sampleRate : 8000;
    const int channelsValue = qMax(1, channels);
    const int bitsValue = 16;
    const int blockAlign = channelsValue * bitsValue / 8;
    const int byteRate = sampleRateValue * blockAlign;
    const int riffSize = 36 + dataSize;

    QByteArray wav;
    wav.reserve(44 + dataSize);
    wav += QByteArrayLiteral("RIFF");
    char buffer[4];

    auto appendLE32 = [&](quint32 value) {
        qToLittleEndian<quint32>(value, buffer);
        wav.append(buffer, 4);
    };
    auto appendLE16 = [&](quint16 value) {
        qToLittleEndian<quint16>(value, buffer);
        wav.append(buffer, 2);
    };

    appendLE32(static_cast<quint32>(riffSize));
    wav += QByteArrayLiteral("WAVE");
    wav += QByteArrayLiteral("fmt ");
    appendLE32(16);
    appendLE16(1); // PCM
    appendLE16(static_cast<quint16>(channelsValue));
    appendLE32(static_cast<quint32>(sampleRateValue));
    appendLE32(static_cast<quint32>(byteRate));
    appendLE16(static_cast<quint16>(blockAlign));
    appendLE16(static_cast<quint16>(bitsValue));
    wav += QByteArrayLiteral("data");
    appendLE32(static_cast<quint32>(dataSize));
    wav += pcm;
    return wav;
}
