#include "dax/daximagefile.h"

#include "dax/renderblocks.h"

DaxImageFile::DaxImageFile(const QString &path)
    : m_file(path)
{
    if (!m_file.isValid())
        return;

    for (const DaxFileBlock &block : m_file.blocks()) {
        FileBlockParameters parameters;
        parameters.data = block.data;
        parameters.name = block.file;
        parameters.id = block.id;

        auto renderBlock = RenderBlockFactory::create(parameters);
        for (const QImage &bitmap : renderBlock->getBitmaps()) {
            m_bitmaps.append(bitmap);
            m_bitmapIds.append(renderBlock->blockId());
        }
    }
}
