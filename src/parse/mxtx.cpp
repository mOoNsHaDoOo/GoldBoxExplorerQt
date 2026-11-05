#include "parse/mxtx.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace {

int be16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return (static_cast<quint8>(data.at(offset)) << 8)
           | static_cast<quint8>(data.at(offset + 1));
}

} // namespace

bool MxtxFile::looksLikeMxtx(const QByteArray &data)
{
    return data.size() >= 16 && data.left(4) == QByteArrayLiteral("MXTX");
}

bool MxtxFile::load(const QString &path)
{
    m_error.clear();
    m_valid = false;
    m_events.clear();
    m_totalTicks = 0;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    m_data = file.readAll();
    if (!looksLikeMxtx(m_data)) {
        m_error = QStringLiteral("Not an MXTX song module.");
        return false;
    }

    m_tempo = be16(m_data, 4);
    m_flags = be16(m_data, 6);
    m_headerCount = be16(m_data, 8);
    m_eventStart = 0x24;
    if (m_eventStart >= m_data.size()) {
        m_error = QStringLiteral("MXTX header is truncated.");
        return false;
    }

    // The event stream is dominated by the 0x00 0x30 (48-tick) separator.
    // The waveform/envelope section that follows contains no such pair.
    int lastMarker = m_data.lastIndexOf(QByteArrayLiteral("\x00\x30"));
    if (lastMarker < m_eventStart)
        lastMarker = m_data.size() - 6;
    m_eventEnd = qMin(m_data.size(), lastMarker + 6);
    m_eventEnd -= (m_eventEnd - m_eventStart) % 6;
    if (m_eventEnd <= m_eventStart)
        m_eventEnd = m_data.size() - ((m_data.size() - m_eventStart) % 6);

    for (int offset = m_eventStart; offset + 6 <= m_eventEnd; offset += 6) {
        const int delta = be16(m_data, offset);
        const int note = static_cast<quint8>(m_data.at(offset + 2));
        const int velocity = static_cast<quint8>(m_data.at(offset + 3));
        const int duration = be16(m_data, offset + 4);
        if (note < 0x20 || note > 0x60 || delta > 0x1000 || duration > 0x1000)
            continue;
        MxtxEvent event;
        event.offset = offset;
        event.delta = delta;
        event.note = note;
        event.velocity = velocity;
        event.duration = duration;
        m_events.append(event);
        m_totalTicks += delta;
    }

    if (m_events.isEmpty()) {
        m_error = QStringLiteral("No MXTX note events were found.");
        return false;
    }

    m_valid = true;
    return true;
}

AudioSample MxtxFile::synthesize(const MxtxFile &file, int outputRate)
{
    AudioSample sample;
    if (!file.isValid() || file.events().isEmpty())
        return sample;
    if (outputRate < 8000)
        outputRate = 44100;

    // The deltas are in milliseconds (48 = 48 ms), which matches the tempo
    // and length of the original cues closely enough for a preview.
    constexpr double secondsPerTick = 0.001;
    const double totalSeconds = qMin(240.0, file.totalTicks() * secondsPerTick + 0.5);
    const int totalSamples = qMax(1, static_cast<int>(totalSeconds * outputRate));
    QVector<float> mix(totalSamples, 0.0f);

    double time = 0.0;
    for (const MxtxEvent &event : file.events()) {
        const int ticks = qMax(1, event.duration);
        const double seconds = ticks * secondsPerTick;
        const double frequency = 440.0 * std::pow(2.0, (event.note - 69) / 12.0);
        const int start = static_cast<int>(time * outputRate);
        const int length = static_cast<int>(seconds * outputRate);
        const int end = qMin(totalSamples, start + length);
        const int attack = qMin(length / 4, outputRate / 200);
        const int release = qMin(length / 3, outputRate / 35);
        double phase = 0.0;
        for (int i = start; i < end; ++i) {
            const float value = (phase < 0.5) ? 0.28f : -0.28f;
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
        time += event.delta * secondsPerTick;
    }

    float peak = 0.0f;
    for (float value : mix)
        peak = std::max(peak, std::fabs(value));
    const float scale = peak > 0.8f ? 0.8f / peak : 1.0f;

    sample.name = QStringLiteral("MXTX song");
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
