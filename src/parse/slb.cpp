#include "parse/slb.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace {

int be16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return -1;
    return (static_cast<quint8>(data.at(offset)) << 8)
           | static_cast<quint8>(data.at(offset + 1));
}

} // namespace

bool SlbFile::looksLikeSlb(const QByteArray &data)
{
    if (data.size() < 16 || data.left(4) != QByteArrayLiteral("SLBR"))
        return false;
    const int sizeBe = (static_cast<quint8>(data.at(4)) << 24)
                       | (static_cast<quint8>(data.at(5)) << 16)
                       | (static_cast<quint8>(data.at(6)) << 8)
                       | static_cast<quint8>(data.at(7));
    const int sizeLe = static_cast<quint8>(data.at(4))
                       | (static_cast<quint8>(data.at(5)) << 8)
                       | (static_cast<quint8>(data.at(6)) << 16)
                       | (static_cast<quint8>(data.at(7)) << 24);
    return sizeBe == data.size() || sizeLe == data.size();
}

bool SlbFile::load(const QString &path)
{
    m_error.clear();
    m_valid = false;
    m_songs.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();
    if (!looksLikeSlb(data)) {
        m_error = QStringLiteral("Not an SLBR song bank.");
        return false;
    }
    m_size = data.size();

    int firstEnd = data.indexOf(QByteArrayLiteral("\xff\xff"), 10);
    if (firstEnd < 0) {
        m_error = QStringLiteral("No song terminator found.");
        return false;
    }

    // Locate the first song stream.  The header length varies slightly between
    // games, so try the first few even offsets and keep the first one that
    // parses cleanly up to the first 0xff 0xff terminator.
    int dataStart = -1;
    for (int candidate = 0x10; candidate <= qMin(0x50, firstEnd); candidate += 2) {
        if (candidate + 2 > firstEnd)
            break;
        const int first = static_cast<quint8>(data.at(candidate));
        if (first < 0x80)
            continue;
        int pos = candidate;
        bool ok = true;
        while (pos + 2 <= firstEnd + 2) {
            const quint8 a = static_cast<quint8>(data.at(pos));
            const quint8 b = static_cast<quint8>(data.at(pos + 1));
            if (a == 0xFF && b == 0xFF) {
                if (pos + 2 != firstEnd + 2)
                    ok = false;
                break;
            }
            if (a == 0xFF) {
                ok = false;
                break;
            }
            pos += 2;
        }
        if (ok && pos + 2 == firstEnd + 2) {
            dataStart = candidate;
            break;
        }
    }
    if (dataStart < 0)
        dataStart = (firstEnd > 0x1c) ? 0x1c : 0x10;

    QVector<int> headerValues;
    for (int offset = 0x0c; offset + 2 <= dataStart; offset += 2) {
        const int value = be16(data, offset);
        if (value > 0 && value < m_size)
            headerValues.append(value);
    }

    int pos = dataStart;
    int index = 0;
    while (pos + 2 <= m_size) {
        const int end = data.indexOf(QByteArrayLiteral("\xff\xff"), pos);
        if (end < 0)
            break;

        SlbSong song;
        song.index = index;
        song.start = pos;
        song.end = end + 2;

        int cursor = pos;
        while (cursor + 2 <= song.end) {
            SlbEvent event;
            event.offset = cursor;
            event.command = static_cast<quint8>(data.at(cursor));
            event.data = static_cast<quint8>(data.at(cursor + 1));
            event.duration = event.data & 0x3F;
            if (event.command == 0xFF && event.data == 0xFF) {
                event.end = true;
            } else if (event.command < 0x80) {
                event.note = true;
                song.noteCount += 1;
                song.totalUnits += qMax(1, event.duration);
            } else if (event.command == 0x80) {
                event.rest = true;
                song.totalUnits += qMax(1, event.duration);
            } else {
                song.commandCount += 1;
            }
            song.events.append(event);
            cursor += 2;
        }

        for (int value : headerValues) {
            if (value > song.start && value < song.end)
                song.loopPoint = qMax(song.loopPoint, value);
        }

        m_songs.append(song);
        index += 1;

        pos = end + 2;
        while (pos + 1 < m_size
               && static_cast<quint8>(data.at(pos)) == 0
               && static_cast<quint8>(data.at(pos + 1)) == 0)
            pos += 2;
    }

    if (m_songs.isEmpty()) {
        m_error = QStringLiteral("No song streams could be decoded.");
        return false;
    }
    m_valid = true;
    return true;
}

AudioSample SlbFile::synthesize(const SlbSong &song, int outputRate)
{
    AudioSample sample;
    if (song.events.isEmpty())
        return sample;
    if (outputRate < 8000)
        outputRate = 44100;

    double unitSeconds = 0.06;
    for (const SlbEvent &event : song.events) {
        if (event.command == 0x88 && event.data > 0) {
            unitSeconds = 60.0 / (event.data * 16.0);
            break;
        }
    }

    const double totalSeconds = song.totalUnits * unitSeconds + 0.35;
    const int totalSamples = qMax(1, static_cast<int>(totalSeconds * outputRate));
    QVector<float> mix(totalSamples, 0.0f);

    double time = 0.0;
    for (const SlbEvent &event : song.events) {
        const int units = qMax(1, event.duration);
        const double seconds = units * unitSeconds;
        if (event.note) {
            const double frequency = 440.0 * std::pow(2.0, (event.command - 69) / 12.0);
            const int start = static_cast<int>(time * outputRate);
            const int length = static_cast<int>(seconds * outputRate);
            const int end = qMin(totalSamples, start + length);
            const int attack = qMin(length / 4, outputRate / 200);
            const int release = qMin(length / 3, outputRate / 35);
            double phase = 0.0;
            for (int i = start; i < end; ++i) {
                const float value = (phase < 0.5) ? 0.30f : -0.30f;
                float envelope = 1.0f;
                const int local = i - start;
                if (attack > 0 && local < attack)
                    envelope = static_cast<float>(local) / attack;
                else if (release > 0 && local > length - release)
                    envelope = static_cast<float>(length - local) / release;
                mix[i] += value * envelope;
                phase += frequency / outputRate;
                if (phase >= 1.0)
                    phase -= std::floor(phase);
            }
        }
        if (!event.rest && !event.end)
            time += seconds;
    }

    float peak = 0.0f;
    for (float value : mix)
        peak = std::max(peak, std::fabs(value));
    const float scale = peak > 0.8f ? 0.8f / peak : 1.0f;

    sample.name = QStringLiteral("SLB song %1").arg(song.index + 1);
    sample.sampleRate = outputRate;
    sample.channels = 1;
    sample.bits = 16;
    sample.frameCount = totalSamples;
    sample.pcm.resize(totalSamples * 2);
    for (int i = 0; i < totalSamples; ++i) {
        const qint16 value = static_cast<qint16>(
            qBound(-32767.0f, mix.at(i) * scale * 32767.0f, 32767.0f));
        qToLittleEndian<qint16>(value, sample.pcm.data() + i * 2);
    }
    sample.valid = true;
    return sample;
}
