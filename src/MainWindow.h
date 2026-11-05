#pragma once

#include <QMainWindow>
#include <QSettings>

#include <memory>

class QLabel;
class QLineEdit;
class QSplitter;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class Plugin;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Used by the smoke-test entry point; loads a folder and the given file.
    void openForTesting(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void initDirectory(const QString &path);
    void loadDirectory(const QString &path);
    void loadFile(const QString &path);
    void showViewer(QWidget *viewer);
    void setZoom(float zoom);
    void zoomIn();
    void zoomOut();
    void updateZoomLabel();
    void updateViewerWidth();
    void addDirectories(QTreeWidgetItem *item, const QString &path);
    void selectRootFolder();
    void exportCurrentFile();
    void showAbout();

    QTreeWidget *m_folderView = nullptr;
    QTableWidget *m_fileView = nullptr;
    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_rightSplitter = nullptr;
    QWidget *m_viewerHost = nullptr;
    QVBoxLayout *m_viewerLayout = nullptr;
    QWidget *m_currentViewer = nullptr;
    QLineEdit *m_directoryEdit = nullptr;
    QLabel *m_zoomLabel = nullptr;

    QString m_currentDirectory;
    QString m_currentFile;
    QString m_rootFolder;
    std::unique_ptr<Plugin> m_plugin;
    QSettings m_settings;
    float m_zoom = 1.0f;
};
