#pragma once

#include "parse/audio.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// Music-X song module used by Secret of the Silver Blades.  The file starts
// with "MXTX" followed by a small header, a six-byte event stream, and a
// waveform/envelope section.
struct MxtxEvent
{
    int offset = 0;
    int delta = 0;
    int note = 0;
    int velocity = 0;
    int duration = 0;
};

class MxtxFile
{
public:
    MxtxFile() = default;
    explicit MxtxFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }

    int fileSize() const { return m_data.size(); }
    int tempo() const { return m_tempo; }
    int flags() const { return m_flags; }
    int headerCount() const { return m_headerCount; }
    int eventStart() const { return m_eventStart; }
    int eventEnd() const { return m_eventEnd; }
    int totalTicks() const { return m_totalTicks; }
    const QByteArray &data() const { return m_data; }
    const QVector<MxtxEvent> &events() const { return m_events; }

    static bool looksLikeMxtx(const QByteArray &data);
    static AudioSample synthesize(const MxtxFile &file, int outputRate = 44100);

private:
    QString m_error;
    bool m_valid = false;
    int m_tempo = 0;
    int m_flags = 0;
    int m_headerCount = 0;
    int m_eventStart = 0x24;
    int m_eventEnd = 0;
    int m_totalTicks = 0;
    QByteArray m_data;
    QVector<MxtxEvent> m_events;
};
