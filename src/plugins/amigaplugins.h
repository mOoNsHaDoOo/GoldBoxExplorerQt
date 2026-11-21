#pragma once

#include "plugins/plugin.h"

// Standard AmigaOS files found on the game disks (not game data): Workbench
// icons (.info, DiskObject) and hunk executables (c/, l/, hdinstall).
// Both formats are the documented AmigaOS ones.
class AmigaSystemPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    bool isImageFile() const override { return !m_images.isEmpty(); }
    QVector<QImage> getBitmaps() override { return m_images; }

private:
    QVector<QImage> m_images;
};
