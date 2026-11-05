#pragma once

#include "core/daxfile.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

struct EclBlock
{
    int id = 0;
    QByteArray data;
    QStringList strings;
};

class EclFile
{
public:
    explicit EclFile(const QString &path);

    bool isValid() const { return m_file.isValid(); }
    QString errorString() const { return m_file.errorString(); }
    const QVector<EclBlock> &blocks() const { return m_blocks; }
    QString allText() const;

    // Extracts the readable strings embedded in a single ECL block.  Also
    // used by the GLIB container viewer (ECL.GLB).
    static QStringList extractStrings(const QByteArray &data);

private:
    static QString decompressString(const QByteArray &data);
    static bool isReadableText(const QString &text);

    DaxFile m_file;
    QVector<EclBlock> m_blocks;
};
