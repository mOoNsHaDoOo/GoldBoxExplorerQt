#pragma once

#include "plugins/plugin.h"

#include <memory>

namespace PluginFactory {
// Plugin for a file. Multi-block files (DAX, DAA, GLB, TLB) are wrapped in a
// ContainerPlugin that adds the block browser around the normal viewer.
std::unique_ptr<Plugin> create(const QString &path);

// The plugin selected by file name/content alone, without the block browser.
std::unique_ptr<Plugin> createForFile(const QString &path);
}
