#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

// Minimal IFF reader used for Amiga-era Gold Box assets: 8SVX/16SV audio,
// ILBM/PBM images, and the base frame of IFF ANIM files. Game-specific
// containers such as .DAA are inspected for these forms as well.
struct IffChunk
{
    QByteArray id;
    QByteArray data;
};

struct IffForm
{
    QByteArray type;
    QVector<IffChunk> chunks;
};

bool readIffForm(const QByteArray &data, IffForm &form, QString *error = nullptr);
bool looksLikeIff(const QByteArray &data, const QByteArray &type = {});

// Decodes an ILBM/PBM/ANIM base frame. Nested FORM chunks are searched too.
QImage decodeIffImage(const QByteArray &data, QString *error = nullptr);
