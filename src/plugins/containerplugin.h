#pragma once

#include "core/container.h"
#include "plugins/plugin.h"

#include <QPointer>

#include <memory>

class QLabel;
class QObject;
class QSplitter;
class QTableWidget;
class QTemporaryDir;
class QVBoxLayout;
class ImageSheetView;

// Wraps the normal plugin of a multi-block file (DAX, DAA, GLB, TLB) with a
// block browser: a table of the inner blocks on the left and, on the right,
// either the usual whole-file viewer ("All blocks" row) or the selected block
// shown by the plugin that matches the file name. The block is handed to that
// plugin as a single-entry container written to a temporary mirror of the
// file's folder (the other files of the folder are linked there, so viewers
// that look for companion files keep working).
class ContainerPlugin : public Plugin
{
public:
    explicit ContainerPlugin(Container container);
    ~ContainerPlugin() override;

    bool isSatisfiedBy(const QString &path) const override;
    QWidget *createViewer(const PluginParameters &parameters) override;
    void setZoom(float zoom) override;
    void setContainerWidth(int width) override;
    bool isImageFile() const override;
    QVector<QImage> getBitmaps() override;
    QVector<int> getBitmapIds() override;

    const Container &container() const { return m_container; }
    // Selects a table row (0 = all blocks, n = entry n-1). Used by tests.
    void selectRow(int row);

private:
    void showRow(int row);
    QWidget *createEntryViewer(int index);
    QString writeSingleEntryFile(int index);
    void exportEntry(int index, bool decoded);
    int viewerWidth() const;

    Container m_container;
    std::unique_ptr<Plugin> m_wholePlugin;
    std::unique_ptr<Plugin> m_entryPlugin;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<QObject> m_guard;
    QVector<QImage> m_entryImages;
    QPointer<QTableWidget> m_table;
    QPointer<QWidget> m_wholeViewer;
    QPointer<QWidget> m_entryViewer;
    QPointer<QWidget> m_right;
    QVBoxLayout *m_rightLayout = nullptr;
    QPointer<ImageSheetView> m_entrySheet;
    PluginParameters m_parameters;
    int m_currentRow = 0;
    float m_zoom = 1.0f;
    int m_containerWidth = 800;
};
