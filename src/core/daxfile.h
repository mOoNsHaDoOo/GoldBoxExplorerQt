#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct DaxFileBlock
{
    QString file;
    int id = 0;
    QByteArray data;
};

// Reader for the "DAX" container format used by SSI Gold Box games.
class DaxFile
{
public:
    DaxFile() = default;
    explicit DaxFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    bool isAmiga() const { return m_amiga; }
    QString fileName() const { return m_fileName; }
    QString errorString() const { return m_error; }

    const QVector<DaxFileBlock> &blocks() const { return m_blocks; }
    const DaxFileBlock *blockById(int id) const;

    QByteArray allBytes() const;

    // SSI's run-length decoder. Public because the sprite renderer uses it.
    static QByteArray decode(const QByteArray &input, int outputSize);

private:
    QString m_fileName;
    QString m_error;
    QVector<DaxFileBlock> m_blocks;
    bool m_valid = false;
    bool m_amiga = false;
};
