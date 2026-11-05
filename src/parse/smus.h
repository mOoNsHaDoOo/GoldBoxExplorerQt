#pragma once

#include "parse/audio.h"

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

struct SmusEvent
{
    int sid = 0;
    int data = 0;
};

struct SmusTrack
{
    QVector<SmusEvent> events;
};

struct SmusScore
{
    bool valid = false;
    QString name;
    int tempo = 12800; // 128ths of a quarter note per minute
    int volume = 127;
    int trackCount = 0;
    QMap<int, QString> instrumentNames; // INS1 register -> name
    QVector<SmusTrack> tracks;
};

struct SmusBank
{
    QHash<QString, AudioSample> samples;       // sample name -> 8SVX
    QHash<QString, QString> instrumentSamples; // INST name -> SAMP name
};

bool loadSmus(const QByteArray &data, SmusScore &score, QString *error = nullptr);
bool loadSmusBank(const QByteArray &data, SmusBank &bank, QString *error = nullptr);

// Renders a rough but useful preview: each note uses the instrument's sample
// as a looping wavetable, with a short attack/release envelope. This is not a
// cycle-accurate Paula emulation, but it makes the SMUS score audible with the
// correct sample and instrument assignments.
AudioSample synthesizeSmus(const SmusScore &score, const SmusBank &bank,
                           int outputRate = 44100);
