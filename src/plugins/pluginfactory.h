#pragma once

#include "plugins/plugin.h"

#include <memory>

namespace PluginFactory {
std::unique_ptr<Plugin> create(const QString &path);
}
