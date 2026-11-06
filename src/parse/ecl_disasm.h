#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct EclDisasmLine
{
    int address = 0;
    QString text;
    QString annotation;
};

struct EclDisasmResult
{
    bool valid = false;
    QString error;
    QString listing;
    QVector<EclDisasmLine> lines;
};

// Decodes one ECL block.  The block data starts with a two-byte header that
// EclData skips; the VM address space starts at 0x8000 (0x9900 in Pool of
// Radiance).  Control flow is followed through GOTO/GOSUB/ON GOTO targets.
EclDisasmResult disassembleEclBlock(const QByteArray &blockData, bool poolOfRadiance = false);
