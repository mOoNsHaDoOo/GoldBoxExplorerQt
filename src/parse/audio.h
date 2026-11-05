#pragma once

#include <QByteArray>
#include <QString>

struct AudioSample
{
    QString name;
    int sampleRate = 0;
    int channels = 1;
    int bits = 8;
    int frameCount = 0;
    QByteArray pcm; // signed 16-bit little-endian mono
    bool valid = false;
    QString error;

    qint64 durationMs() const
    {
        if (sampleRate <= 0)
            return 0;
        return (static_cast<qint64>(frameCount) * 1000) / sampleRate;
    }

    QByteArray toWav() const;
};

bool load8svx(const QByteArray &data, AudioSample &sample, QString *error = nullptr);
bool looksLike8svx(const QByteArray &data);
