#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

#include <memory>

struct FileBlockParameters
{
    QByteArray data;
    QString name;
    int id = 0;
};

class RenderBlock
{
public:
    virtual ~RenderBlock() = default;

    int blockId() const { return m_blockId; }
    void setBlockId(int id) { m_blockId = id; }
    virtual QVector<QImage> getBitmaps() const { return m_bitmaps; }

protected:
    QVector<QImage> m_bitmaps;
    int m_blockId = 0;
};

class EgaSpriteBlock : public RenderBlock
{
public:
    explicit EgaSpriteBlock(const FileBlockParameters &parameters);
};

class VgaStrataBlock : public RenderBlock
{
public:
    explicit VgaStrataBlock(const FileBlockParameters &parameters);
    static int getDataOffset(const QByteArray &input, int offset, QByteArray &paletteOut);
};

class VgaSpriteBlock : public RenderBlock
{
public:
    explicit VgaSpriteBlock(const FileBlockParameters &parameters);
    static QByteArray unpackSpriteData(const QByteArray &data, int offset);
};

class VgaMixedBlock : public RenderBlock
{
public:
    explicit VgaMixedBlock(const FileBlockParameters &parameters);
    static int decodePaletteData(QByteArray &target, int offset, const QByteArray &input);
};

class VgaBlock : public RenderBlock
{
public:
    explicit VgaBlock(const FileBlockParameters &parameters);
};

class EgaBlock : public RenderBlock
{
public:
    explicit EgaBlock(const FileBlockParameters &parameters);
};

class MonoBlock : public RenderBlock
{
public:
    explicit MonoBlock(const FileBlockParameters &parameters);
};

class UnknownBlock : public RenderBlock
{
};

class RenderBlockFactory
{
public:
    static std::unique_ptr<RenderBlock> create(const FileBlockParameters &parameters);
};
