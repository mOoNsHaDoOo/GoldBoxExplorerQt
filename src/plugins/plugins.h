#pragma once

#include "plugins/plugin.h"

#include "dax/daximagefile.h"
#include "dax/walldef.h"
#include "parse/daa.h"
#include "tbl/tlbfile.h"

#include <QString>
#include <QVector>
#include <memory>

class QScrollArea;
class QTabWidget;
class QLabel;
class ImageSheetView;
class GeoDaxView;

class DaxImagePlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    void setZoom(float zoom) override;
    void setContainerWidth(int width) override;
    bool isImageFile() const override { return true; }
    QVector<QImage> getBitmaps() override;
    QVector<int> getBitmapIds() override;

private:
    std::unique_ptr<DaxImageFile> m_file;
    ImageSheetView *m_sheet = nullptr;
    QScrollArea *m_scroll = nullptr;
    float m_zoom = 1.0f;
    int m_containerWidth = 800;
};

class DaxWallDefPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    void setZoom(float zoom) override;
    void setContainerWidth(int width) override;
    bool isImageFile() const override { return true; }
    QVector<QImage> getBitmaps() override;
    QVector<int> getBitmapIds() override;

private:
    std::unique_ptr<DaxWallDefFile> m_file;
    QVector<ImageSheetView *> m_sheets;
    float m_zoom = 1.0f;
    int m_containerWidth = 800;
};

class TlbPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    void setZoom(float zoom) override;
    void setContainerWidth(int width) override;
    bool isImageFile() const override { return true; }
    QVector<QImage> getBitmaps() override;

private:
    std::unique_ptr<TlbFile> m_file;
    ImageSheetView *m_sheet = nullptr;
    float m_zoom = 1.0f;
    int m_containerWidth = 800;
};

class TextPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class ImagePlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    bool isImageFile() const override { return true; }
    QVector<QImage> getBitmaps() override;

private:
    QImage m_image;
};

class HexPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class GameDataPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class SavedGamePlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class VaultPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class ItemsPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class GlbPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class CharacterPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

// Item/effect records stored inside Amiga DAX containers (ITEM*.DAX,
// MON*ITM.DAX, MON*SPC.DAX). The original character plugin only enabled this
// for Pool of Radiance; the same record layouts are useful in the Krynn games.
class DaxRecordPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class GeoPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class EclPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

class GeoDaxPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    void setZoom(float zoom) override;
    void setContainerWidth(int width) override;

private:
    GeoDaxView *m_view = nullptr;
    float m_zoom = 1.0f;
};

class AudioPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

// Generic IFF inspector for extensionless Amiga files such as Sound/cast4
// (FORM INST) and Sound/krynn (FORM SMUS), plus any other IFF form.
class IffPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    bool isImageFile() const override { return m_isImage; }
    QVector<QImage> getBitmaps() override;

private:
    QVector<QImage> m_images;
    bool m_isImage = false;
};

// Bank of multiple IFF forms, as used by Sound/soundData (many 8SVX samples
// plus INST/ENVL definitions).
class SoundBankPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

// Amiga save/character side files: CHRDATA*.sav/.swg/.fx and SAVGAM*.DAT.
class SaveFilePlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

// Period table used by the Amiga music engine (Sound/periodTable).
class PeriodTablePlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

// Amiga "items" table (2-byte header + 256 eight-byte item templates).
class AmigaItemsPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
};

// Preview for Gold Box .DAA/.DAI files. The format is container-like, so the
// plugin tries IFF (ILBM/PBM/8SVX/ANIM) and DAX content before falling back to
// a structural summary and hex view.
class DaaPlugin : public Plugin
{
public:
    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    void setZoom(float zoom) override;
    void setContainerWidth(int width) override;
    bool isImageFile() const override { return m_isImage; }
    QVector<QImage> getBitmaps() override;
    QVector<int> getBitmapIds() override;

private:
    QImage m_image;
    std::unique_ptr<DaxImageFile> m_dax;
    std::unique_ptr<DaaFile> m_daa;
    QVector<QImage> m_daaImages;
    QVector<int> m_daaIds;
    ImageSheetView *m_sheet = nullptr;
    bool m_isImage = false;
    float m_zoom = 1.0f;
    int m_containerWidth = 800;
};
