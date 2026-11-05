#include "plugins/plugins.h"

#include <QFileInfo>

#include <memory>

namespace {

template<typename T>
bool matches(const QString &path)
{
    T plugin;
    return plugin.isSatisfiedBy(path);
}

} // namespace

namespace PluginFactory {

std::unique_ptr<Plugin> create(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() == 0)
        return nullptr;

    // Keep the same order as PluginFactory in the original application.
    if (matches<EclPlugin>(path))
        return std::make_unique<EclPlugin>();
    if (matches<GeoDaxPlugin>(path))
        return std::make_unique<GeoDaxPlugin>();
    if (matches<DaxRecordPlugin>(path))
        return std::make_unique<DaxRecordPlugin>();
    if (matches<DaxImagePlugin>(path))
        return std::make_unique<DaxImagePlugin>();
    if (matches<DaxWallDefPlugin>(path))
        return std::make_unique<DaxWallDefPlugin>();
    if (matches<CharacterPlugin>(path))
        return std::make_unique<CharacterPlugin>();
    if (matches<GameDataPlugin>(path))
        return std::make_unique<GameDataPlugin>();
    if (matches<GeoPlugin>(path))
        return std::make_unique<GeoPlugin>();
    if (matches<GlbPlugin>(path))
        return std::make_unique<GlbPlugin>();
    if (matches<ItemsPlugin>(path))
        return std::make_unique<ItemsPlugin>();
    if (matches<SavedGamePlugin>(path))
        return std::make_unique<SavedGamePlugin>();
    if (matches<VaultPlugin>(path))
        return std::make_unique<VaultPlugin>();
    if (matches<TlbPlugin>(path))
        return std::make_unique<TlbPlugin>();
    if (matches<AudioPlugin>(path))
        return std::make_unique<AudioPlugin>();
    if (matches<DaaPlugin>(path))
        return std::make_unique<DaaPlugin>();
    if (matches<IffPlugin>(path))
        return std::make_unique<IffPlugin>();
    if (matches<SoundBankPlugin>(path))
        return std::make_unique<SoundBankPlugin>();
    if (matches<SaveFilePlugin>(path))
        return std::make_unique<SaveFilePlugin>();
    if (matches<PeriodTablePlugin>(path))
        return std::make_unique<PeriodTablePlugin>();
    if (matches<AmigaItemsPlugin>(path))
        return std::make_unique<AmigaItemsPlugin>();
    if (matches<FontPlugin>(path))
        return std::make_unique<FontPlugin>();
    if (matches<SlbPlugin>(path))
        return std::make_unique<SlbPlugin>();
    if (matches<ImagePlugin>(path))
        return std::make_unique<ImagePlugin>();
    if (matches<TextPlugin>(path))
        return std::make_unique<TextPlugin>();

    return std::make_unique<HexPlugin>();
}

} // namespace PluginFactory
