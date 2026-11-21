#pragma once

#include <QByteArray>
#include <QString>

// "ECL Basic": the cleartext listing of a Champions of Krynn (Amiga) ECL
// program.  One command per line in execution order, no addresses; labels
// only where something jumps (GOTO, GOSUB, ON_GOTO, ON_GOSUB and the five
// header entries) and tables for the bytes that GET_TABLE/SET_TABLE read.
//
// This is a port of disassemble() in the campaign tool ecl_basic.py (with
// the decoder of ecl_codec.py) and produces byte-identical text.
//
// The program image is the ECL block without its first two bytes, as the
// game loads it at the logical address $3500.  A block that is not in this
// dialect (no header of five GOTOs, an unknown opcode, an operand outside the
// program, ...) yields ok == false and a one-line error.
struct EclBasicResult
{
    bool ok = false;
    QString text;
    QString error;
};

EclBasicResult eclBasicDisassemble(const QByteArray &programImage,
                                   const QString &fileTag, int blockId);

// Program image of a raw ECL block (load_ecl_program: drops the first 2 bytes).
QByteArray eclBasicProgramOfBlock(const QByteArray &block);

// Same as eclBasicDisassemble(eclBasicProgramOfBlock(block), ...).
EclBasicResult eclBasicDisassembleBlock(const QByteArray &block,
                                        const QString &fileTag, int blockId);

// "ECL1" for ".../ecl1.dax": the file tag used in the "program" line.
QString eclBasicFileTag(const QString &path);

// Description of a variable name of the symbol table ("" when unknown).
QString eclBasicSymbolDescription(const QString &name);
