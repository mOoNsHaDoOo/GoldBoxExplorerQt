#include "parse/smus.h"

#include "core/byteutils.h"
#include "parse/iff.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace {

quint16 be16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}

quint32 be32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

QString cleanText(const QByteArray &data)
{
    int end = data.indexOf('\0');
    if (end < 0)
        end = data.size();
    return QString::fromLatin1(data.left(end)).trimmed();
}

QByteArray chunkData(const IffForm &form, const QByteArray &id)
{
    for (const IffChunk &chunk : form.chunks) {
        if (chunk.id == id)
            return chunk.data;
    }
    return {};
}

QString normalized(const QString &text)
{
    return text.trimmed().toLower();
}

double noteDuration(int data, double wholeSeconds)
{
    const int division = data & 0x07;
    const bool dotted = (data & 0x08) != 0;
    const int tuplet = (data >> 4) & 0x03;

    double fraction = 1.0 / static_cast<double>(1 << division);
    if (dotted)
        fraction *= 1.5;
    static const double tupletFactor[4] = {1.0, 2.0 / 3.0, 4.0 / 5.0, 6.0 / 7.0};
    fraction *= tupletFactor[qBound(0, tuplet, 3)];
    return fraction * wholeSeconds;
}

} // namespace

bool loadSmus(const QByteArray &data, SmusScore &score, QString *error)
{
    score = {};
    IffForm form;
    if (!readIffForm(data, form, error))
        return false;
    if (form.type != QByteArrayLiteral("SMUS")) {
        if (error)
            *error = QStringLiteral("Not an IFF SMUS score.");
        return false;
    }

    const QByteArray shdr = chunkData(form, QByteArrayLiteral("SHDR"));
    if (shdr.size() >= 4) {
        score.tempo = be16(shdr, 0);
        score.volume = byteAt(shdr, 2);
        score.trackCount = byteAt(shdr, 3);
    }
    score.name = cleanText(chunkData(form, QByteArrayLiteral("NAME")));

    for (const IffChunk &chunk : form.chunks) {
        if (chunk.id == QByteArrayLiteral("INS1") && chunk.data.size() >= 4) {
            const int reg = byteAt(chunk.data, 0);
            score.instrumentNames.insert(reg, cleanText(chunk.data.mid(4)));
        } else if (chunk.id == QByteArrayLiteral("TRAK")) {
            SmusTrack track;
            for (int i = 0; i + 1 < chunk.data.size(); i += 2)
                track.events.append({byteAt(chunk.data, i), byteAt(chunk.data, i + 1)});
            score.tracks.append(track);
        }
    }

    if (score.tracks.isEmpty()) {
        if (error)
            *error = QStringLiteral("SMUS score has no TRAK chunks.");
        return false;
    }
    score.valid = true;
    return true;
}

bool loadSmusBank(const QByteArray &data, SmusBank &bank, QString *error)
{
    bank = {};
    int pos = 0;
    int found = 0;

    while (pos + 12 <= data.size()) {
        if (data.mid(pos, 4) != QByteArrayLiteral("NAME")) {
            ++pos;
            continue;
        }

        const quint32 nameSize = be32(data, pos + 4);
        if (nameSize > 1024 || pos + 8 + static_cast<int>(nameSize) > data.size()) {
            ++pos;
            continue;
        }
        const QString name = cleanText(data.mid(pos + 8, static_cast<int>(nameSize)));
        int next = pos + 8 + static_cast<int>(nameSize) + static_cast<int>(nameSize & 1);
        if (next + 12 > data.size() || data.mid(next, 4) != QByteArrayLiteral("FORM")) {
            ++pos;
            continue;
        }

        const quint32 formSize = be32(data, next + 4);
        if (next + 8 + static_cast<int>(formSize) > data.size()) {
            ++pos;
            continue;
        }
        const QByteArray formData = data.mid(next, 8 + static_cast<int>(formSize));
        IffForm form;
        if (readIffForm(formData, form)) {
            if (form.type == QByteArrayLiteral("8SVX") || form.type == QByteArrayLiteral("16SV")) {
                AudioSample sample;
                QString audioError;
                if (load8svx(formData, sample, &audioError)) {
                    sample.name = name;
                    bank.samples.insert(normalized(name), sample);
                    ++found;
                }
            } else if (form.type == QByteArrayLiteral("INST")) {
                const QByteArray samp = chunkData(form, QByteArrayLiteral("SAMP"));
                const QString sampleName = cleanText(samp);
                if (!sampleName.isEmpty()) {
                    bank.instrumentSamples.insert(normalized(name), normalized(sampleName));
                    ++found;
                }
            }
        }

        // Continue scanning one byte at a time rather than jumping to the
        // end of the form: a few Amiga bank files have a NAME chunk that
        // starts immediately after an odd-sized form without its pad byte.
        ++pos;
    }

    if (found == 0) {
        if (error)
            *error = QStringLiteral("No 8SVX samples or INST definitions found.");
        return false;
    }
    return true;
}

AudioSample synthesizeSmus(const SmusScore &score, const SmusBank &bank, int outputRate)
{
    AudioSample result;
    if (!score.valid || score.tracks.isEmpty())
        return result;
    if (outputRate < 8000)
        outputRate = 44100;

    const double qpm = std::max(1.0, score.tempo / 128.0);
    double wholeSeconds = 4.0 * 60.0 / qpm;

    struct Note
    {
        double start = 0;
        double duration = 0;
        int tone = 60;
        int instrument = 0;
        int dynamic = 127;
    };
    QVector<Note> notes;
    double maximumTime = 0;

    for (int trackIndex = 0; trackIndex < score.tracks.size(); ++trackIndex) {
        const SmusTrack &track = score.tracks.at(trackIndex);
        double time = 0;
        int instrument = trackIndex;
        int dynamic = 127;
        double currentWhole = wholeSeconds;

        for (const SmusEvent &event : track.events) {
            if (event.sid < 128 || event.sid == 128) {
                const double duration = noteDuration(event.data, currentWhole);
                if (event.sid < 128) {
                    notes.append({time, std::min(duration, 10.0),
                                  event.sid, instrument, dynamic});
                }
                if ((event.data & 0x80) == 0 || event.sid == 128)
                    time += duration;
            } else if (event.sid == 129) {
                instrument = event.data;
            } else if (event.sid == 132) {
                dynamic = qBound(0, event.data, 127);
            } else if (event.sid == 136 && event.data > 0) {
                currentWhole = 4.0 * 60.0 / static_cast<double>(event.data);
            }
        }
        maximumTime = std::max(maximumTime, time);
    }

    if (notes.isEmpty() || maximumTime <= 0)
        return result;

    maximumTime = std::min(maximumTime + 0.2, 120.0);
    const int totalSamples = static_cast<int>(maximumTime * outputRate);
    if (totalSamples <= 0)
        return result;

    QVector<float> mix(totalSamples, 0.0f);
    QStringList sampleKeys = bank.samples.keys();
    std::sort(sampleKeys.begin(), sampleKeys.end());

    for (const Note &note : notes) {
        const QString instrumentName = score.instrumentNames.value(note.instrument);
        QString sampleKey = bank.instrumentSamples.value(normalized(instrumentName));
        if (sampleKey.isEmpty())
            sampleKey = normalized(instrumentName);
        AudioSample sample = bank.samples.value(sampleKey);
        if (!sample.valid && !sampleKeys.isEmpty())
            sample = bank.samples.value(sampleKeys.at(note.instrument % sampleKeys.size()));

        const int start = static_cast<int>(note.start * outputRate);
        const int length = static_cast<int>(note.duration * outputRate);
        if (length <= 0 || start >= totalSamples)
            continue;
        const int end = std::min(totalSamples, start + length);

        const double frequency = 440.0 * std::pow(2.0, (note.tone - 69) / 12.0);
        const double phaseStep = frequency / outputRate;
        double phase = 0.0;

        const int frameCount = sample.valid ? sample.pcm.size() / 2 : 0;
        const float baseAmplitude = static_cast<float>(note.dynamic / 127.0
                                                        * score.volume / 127.0 * 0.30);
        const int attack = std::min(length / 3, outputRate / 200);
        const int release = std::min(length / 3, outputRate / 30);

        for (int i = start; i < end; ++i) {
            float value = 0.0f;
            if (frameCount > 0) {
                const double tablePos = phase * frameCount;
                const int index = qBound(0, static_cast<int>(tablePos), frameCount - 1);
                const qint16 raw = qFromLittleEndian<qint16>(
                    sample.pcm.constData() + index * 2);
                value = raw / 32768.0f;
            } else {
                value = static_cast<float>(std::sin(phase * 2.0 * M_PI));
            }

            float envelope = 1.0f;
            const int local = i - start;
            if (attack > 0 && local < attack)
                envelope = static_cast<float>(local) / attack;
            else if (release > 0 && local > length - release)
                envelope = static_cast<float>(length - local) / release;

            mix[i] += value * envelope * baseAmplitude;
            phase += phaseStep;
            if (phase >= 1.0)
                phase -= std::floor(phase);
        }
    }

    float peak = 0.0f;
    for (float value : mix)
        peak = std::max(peak, std::fabs(value));
    const float scale = peak > 0.8f ? 0.8f / peak : 1.0f;

    result.name = score.name.isEmpty() ? QStringLiteral("SMUS score") : score.name;
    result.sampleRate = outputRate;
    result.channels = 1;
    result.bits = 16;
    result.frameCount = totalSamples;
    result.pcm.resize(totalSamples * 2);
    for (int i = 0; i < totalSamples; ++i) {
        const qint16 value = static_cast<qint16>(
            qBound(-32767.0f, mix.at(i) * scale * 32767.0f, 32767.0f));
        qToLittleEndian<qint16>(value, result.pcm.data() + i * 2);
    }
    result.valid = true;
    return result;
}
