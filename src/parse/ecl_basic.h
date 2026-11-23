#pragma once

#include <QByteArray>
#include <QString>

// "ECL Basic": the cleartext listing of an ECL program of Champions of Krynn,
// Death Knights of Krynn or Pool of Radiance (Amiga).  One command per line in execution order, no addresses; labels
// only where something jumps (GOTO, GOSUB, ON_GOTO, ON_GOSUB and the five
// header entries) and tables for the bytes that GET_TABLE/SET_TABLE read.
//
// This is a port of disassemble() in the campaign tool ecl_basic.py (with
// the decoder of ecl_codec.py) and produces byte-identical text.
//
// Each game has its own dialect (command set, operand counts, addresses):
// Champions of Krynn is the reference (ecl_basic.py); Death Knights of Krynn
// follows h015_ecl_loop.c / h064_ecl_vm.c of its C port and Pool of Radiance
// mod16.c / mod04.c of its port.
//
// The program image is the part of the ECL block that the game loads at the
// logical address of the program ($3500 in the Krynn games, $9900 in Pool of
// Radiance).  A block that is not in the dialect (no header of five GOTOs, an unknown opcode, an operand outside the
// program, ...) yields ok == false and a one-line error.
enum class EclBasicDialect
{
    ChampionsOfKrynn,
    DeathKnightsOfKrynn,
    PoolOfRadiance,
};

struct EclBasicResult
{
    bool ok = false;
    QString text;
    QString error;
    EclBasicDialect dialect = EclBasicDialect::ChampionsOfKrynn;
};

EclBasicResult eclBasicDisassemble(const QByteArray &programImage,
                                   const QString &fileTag, int blockId,
                                   EclBasicDialect dialect = EclBasicDialect::ChampionsOfKrynn);

// Program image of a raw ECL block (load_ecl_program): drops the first 2
// bytes (and keeps at most the size of the game's program buffer).
QByteArray eclBasicProgramOfBlock(const QByteArray &block,
                                  EclBasicDialect dialect = EclBasicDialect::ChampionsOfKrynn);

// Same as eclBasicDisassemble(eclBasicProgramOfBlock(block), ...).
EclBasicResult eclBasicDisassembleBlock(const QByteArray &block,
                                        const QString &fileTag, int blockId,
                                        EclBasicDialect dialect = EclBasicDialect::ChampionsOfKrynn);

// The dialect of the game an ECL file belongs to, from its name and the
// files next to it: "ECL.DAX" is Pool of Radiance; ECL3.DAX, a DKK.CFG, a
// GLOBALS.DAA or a MON1WIZ.DAX in the folder mean Death Knights of Krynn;
// anything else is Champions of Krynn.
EclBasicDialect eclBasicDialectForFile(const QString &path);

// Decodes a block in the dialect of the file; when that fails, tries the
// other dialects and keeps the first that decodes.
EclBasicResult eclBasicDisassembleBlockAuto(const QByteArray &block,
                                            const QString &fileTag, int blockId,
                                            EclBasicDialect preferred);

// "Champions of Krynn", "Death Knights of Krynn", "Pool of Radiance".
QString eclBasicDialectName(EclBasicDialect dialect);

// "ECL1" for ".../ecl1.dax": the file tag used in the "program" line.
QString eclBasicFileTag(const QString &path);

// Description of a variable name of the symbol table ("" when unknown).
QString eclBasicSymbolDescription(const QString &name);
