#include "MainWindow.h"

#include "ExportDialog.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugins.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace {

QString formatSize(qint64 length)
{
    constexpr qint64 scale = 1024;
    static const char *orders[] = {"GB", "MB", "KB", "Bytes"};
    qint64 max = scale * scale * scale;

    for (const char *order : orders) {
        if (length > max) {
            double value = static_cast<double>(length) / static_cast<double>(max);
            QString formatted = QString::number(value, 'f', 2);
            while (formatted.endsWith(QLatin1Char('0')))
                formatted.chop(1);
            if (formatted.endsWith(QLatin1Char('.')))
                formatted.chop(1);
            return QStringLiteral("%1 %2").arg(formatted, QString::fromLatin1(order));
        }
        max /= scale;
    }
    return QStringLiteral("0 Bytes");
}

QString fileType(const QFileInfo &info)
{
    const QString suffix = info.suffix().toUpper();
    if (suffix.isEmpty())
        return QStringLiteral("File");
    return suffix + QStringLiteral(" File");
}

class SortableItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant a = data(Qt::UserRole);
        const QVariant b = other.data(Qt::UserRole);
        if (a.isValid() && b.isValid()) {
            if (a.typeId() == QMetaType::QDateTime && b.typeId() == QMetaType::QDateTime)
                return a.toDateTime() < b.toDateTime();
            if (a.typeId() == QMetaType::LongLong && b.typeId() == QMetaType::LongLong)
                return a.toLongLong() < b.toLongLong();
        }
        return QString::localeAwareCompare(text().toUpper(), other.text().toUpper()) < 0;
    }
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings(QStringLiteral("GoldBoxExplorer"), QStringLiteral("GoldBoxExplorerQt"))
{
    setWindowTitle(QStringLiteral("Gold Box Explorer"));
    resize(1000, 720);
    buildUi();
    buildMenus();

    qApp->installEventFilter(this);

    QString root = m_settings.value(QStringLiteral("rootFolder")).toString();
    if (root.isEmpty() || !QDir(root).exists())
        root = QDir::homePath();
    m_rootFolder = root;
    initDirectory(root);
    updateZoomLabel();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
}

void MainWindow::openForTesting(const QString &path)
{
    const QFileInfo info(path);
    loadDirectory(info.absolutePath());
    loadFile(path);
}

void MainWindow::buildUi()
{
    m_folderView = new QTreeWidget;
    m_folderView->setHeaderHidden(true);
    m_folderView->setMinimumWidth(200);
    m_folderView->setRootIsDecorated(true);

    m_fileView = new QTableWidget;
    m_fileView->setColumnCount(4);
    m_fileView->setHorizontalHeaderLabels({QStringLiteral("Name"),
                                           QStringLiteral("Date modified"),
                                           QStringLiteral("Type"),
                                           QStringLiteral("Size")});
    m_fileView->horizontalHeader()->setStretchLastSection(false);
    m_fileView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fileView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_fileView->verticalHeader()->setVisible(false);
    m_fileView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileView->setAlternatingRowColors(true);
    m_fileView->setSortingEnabled(true);
    m_fileView->sortByColumn(0, Qt::AscendingOrder);
    m_fileView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_viewerHost = new QWidget;
    m_viewerLayout = new QVBoxLayout(m_viewerHost);
    m_viewerLayout->setContentsMargins(0, 0, 0, 0);
    m_viewerLayout->setSpacing(0);

    m_rightSplitter = new QSplitter(Qt::Vertical);
    m_rightSplitter->addWidget(m_viewerHost);
    m_rightSplitter->addWidget(m_fileView);
    m_rightSplitter->setStretchFactor(0, 3);
    m_rightSplitter->setStretchFactor(1, 2);
    m_rightSplitter->setSizes({430, 260});

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->addWidget(m_folderView);
    m_mainSplitter->addWidget(m_rightSplitter);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({240, 760});
    setCentralWidget(m_mainSplitter);

    auto *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addWidget(new QLabel(QStringLiteral("Directory  ")));

    m_directoryEdit = new QLineEdit;
    m_directoryEdit->setReadOnly(true);
    m_directoryEdit->setMinimumWidth(320);
    m_directoryEdit->setToolTip(QStringLiteral("Click to choose a root folder"));
    toolbar->addWidget(m_directoryEdit);
    toolbar->addSeparator();

    QAction *zoomInAction = toolbar->addAction(QStringLiteral("+"));
    zoomInAction->setToolTip(QStringLiteral("Zoom In"));
    QAction *zoomResetAction = toolbar->addAction(QStringLiteral("1:1"));
    zoomResetAction->setToolTip(QStringLiteral("Reset Zoom"));
    QAction *zoomOutAction = toolbar->addAction(QString::fromUtf8("\xE2\x88\x92"));
    zoomOutAction->setToolTip(QStringLiteral("Zoom Out"));

    m_zoomLabel = new QLabel;
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(m_zoomLabel, 1);

    connect(zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
    connect(zoomResetAction, &QAction::triggered, this, [this] { setZoom(1.0f); });

    connect(m_folderView, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) {
                const QString path = item->data(0, Qt::UserRole).toString();
                if (!path.isEmpty())
                    addDirectories(item, path);
            });

    connect(m_folderView, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
                if (!current)
                    return;
                const QString path = current->data(0, Qt::UserRole).toString();
                if (!path.isEmpty())
                    loadDirectory(path);
            });

    connect(m_fileView, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                if (currentRow < 0)
                    return;
                QTableWidgetItem *item = m_fileView->item(currentRow, 0);
                if (!item)
                    return;
                loadFile(QDir(m_currentDirectory).filePath(item->text()));
            });

    connect(m_fileView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QMenu menu(this);
                QAction *exportAction = menu.addAction(QStringLiteral("Export..."));
                exportAction->setEnabled(m_plugin && m_plugin->isImageFile());
                if (menu.exec(m_fileView->viewport()->mapToGlobal(pos)) == exportAction)
                    exportCurrentFile();
            });

    connect(m_mainSplitter, &QSplitter::splitterMoved, this,
            [this] { updateViewerWidth(); });
    connect(m_rightSplitter, &QSplitter::splitterMoved, this,
            [this] { updateViewerWidth(); });
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    QAction *openFolder = fileMenu->addAction(QStringLiteral("&Open Folder..."));
    connect(openFolder, &QAction::triggered, this, &MainWindow::selectRootFolder);

    QAction *exportAction = fileMenu->addAction(QStringLiteral("&Export..."));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportCurrentFile);

    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(QStringLiteral("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    QMenu *zoomMenu = viewMenu->addMenu(QStringLiteral("Zoom"));

    QAction *zoomInAction = zoomMenu->addAction(QStringLiteral("Zoom In"));
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);

    QAction *zoomOutAction = zoomMenu->addAction(QStringLiteral("Zoom Out"));
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);

    zoomMenu->addSeparator();
    const QList<int> percentages = {400, 200, 150, 125, 100, 75, 50};
    for (int percent : percentages) {
        QAction *action = zoomMenu->addAction(QStringLiteral("%1%").arg(percent));
        connect(action, &QAction::triggered, this, [this, percent] {
            setZoom(percent / 100.0f);
        });
    }

    zoomMenu->addSeparator();
    QAction *custom = zoomMenu->addAction(QStringLiteral("Custom..."));
    connect(custom, &QAction::triggered, this, [this] {
        bool ok = false;
        const int percent = QInputDialog::getInt(
            this, QStringLiteral("Set Custom Zoom Level"),
            QStringLiteral("Zoom Level (in percent)"), qRound(m_zoom * 100.0f),
            10, 6400, 10, &ok);
        if (ok)
            setZoom(percent / 100.0f);
    });

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction *about = helpMenu->addAction(QStringLiteral("About Gold Box Explorer"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::selectRootFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose the root folder"), m_rootFolder);
    if (folder.isEmpty())
        return;

    m_rootFolder = folder;
    m_settings.setValue(QStringLiteral("rootFolder"), folder);
    initDirectory(folder);
}

void MainWindow::initDirectory(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return;

    const QString absolute = dir.absolutePath();
    m_folderView->clear();

    auto *root = new QTreeWidgetItem(QStringList(absolute.toUpper()));
    root->setData(0, Qt::UserRole, absolute);
    new QTreeWidgetItem(root); // placeholder so the expand arrow appears
    m_folderView->addTopLevelItem(root);
    root->setExpanded(true);
    addDirectories(root, absolute);
    m_folderView->setCurrentItem(root);
}

void MainWindow::addDirectories(QTreeWidgetItem *item, const QString &path)
{
    if (item->data(0, Qt::UserRole + 1).toBool())
        return;
    item->setData(0, Qt::UserRole + 1, true);

    qDeleteAll(item->takeChildren());

    const QFileInfoList dirs = QDir(path).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : dirs) {
        auto *child = new QTreeWidgetItem(QStringList(info.fileName().toUpper()));
        child->setData(0, Qt::UserRole, info.absoluteFilePath());
        new QTreeWidgetItem(child);
        item->addChild(child);
    }
}

void MainWindow::loadDirectory(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return;

    m_currentDirectory = dir.absolutePath();
    m_currentFile.clear();
    m_plugin.reset();
    showViewer(nullptr);

    m_directoryEdit->setText(m_currentDirectory.toUpper());
    m_directoryEdit->setCursorPosition(0);

    const QFileInfoList files = dir.entryInfoList(
        QDir::Files, QDir::Name | QDir::IgnoreCase);

    const QSignalBlocker blocker(m_fileView);
    m_fileView->setSortingEnabled(false);
    m_fileView->setRowCount(files.size());

    for (int row = 0; row < files.size(); ++row) {
        const QFileInfo &info = files.at(row);

        auto *name = new SortableItem(info.fileName().toUpper());
        auto *date = new SortableItem(QLocale().toString(info.lastModified(), QLocale::ShortFormat));
        date->setData(Qt::UserRole, info.lastModified());
        auto *type = new SortableItem(fileType(info));
        auto *size = new SortableItem(formatSize(info.size()));
        size->setData(Qt::UserRole, static_cast<qlonglong>(info.size()));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_fileView->setItem(row, 0, name);
        m_fileView->setItem(row, 1, date);
        m_fileView->setItem(row, 2, type);
        m_fileView->setItem(row, 3, size);
    }

    m_fileView->setSortingEnabled(true);
    m_fileView->sortByColumn(0, Qt::AscendingOrder);
    m_fileView->clearSelection();
    m_fileView->setCurrentCell(-1, -1);
}

void MainWindow::loadFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return;

    m_currentFile = path;
    m_plugin.reset();
    showViewer(nullptr);

    m_plugin = PluginFactory::create(path);
    if (!m_plugin)
        return;

    PluginParameters parameters;
    parameters.filename = path;
    parameters.zoom = m_zoom;
    parameters.containerWidth = qMax(1, m_viewerHost->width() - 20);

    QWidget *viewer = m_plugin->createViewer(parameters);
    if (viewer)
        showViewer(viewer);

    m_plugin->setZoom(m_zoom);
    updateViewerWidth();
}

void MainWindow::showViewer(QWidget *viewer)
{
    if (m_currentViewer) {
        m_viewerLayout->removeWidget(m_currentViewer);
        m_currentViewer->deleteLater();
        m_currentViewer = nullptr;
    }

    if (!viewer) {
        auto *placeholder = new QLabel(QStringLiteral("Select a file to view it."));
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet(QStringLiteral("color: #777;"));
        viewer = placeholder;
    }

    m_currentViewer = viewer;
    m_viewerLayout->addWidget(viewer);
}

void MainWindow::setZoom(float zoom)
{
    zoom = qBound(0.1f, zoom, 64.0f);
    if (std::fabs(zoom - m_zoom) < 0.0001f)
        return;
    m_zoom = zoom;
    if (m_plugin)
        m_plugin->setZoom(m_zoom);
    updateZoomLabel();
}

void MainWindow::zoomIn()
{
    setZoom(m_zoom + 0.10f);
}

void MainWindow::zoomOut()
{
    const float next = m_zoom - 0.10f;
    if (next <= 0.10f)
        return;
    setZoom(next);
}

void MainWindow::updateZoomLabel()
{
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoom * 100.0f)));
}

void MainWindow::updateViewerWidth()
{
    if (!m_plugin)
        return;
    m_plugin->setContainerWidth(qMax(1, m_viewerHost->width() - 20));
}

void MainWindow::exportCurrentFile()
{
    if (!m_plugin || m_currentFile.isEmpty())
        return;
    if (!m_plugin->isImageFile()) {
        QMessageBox::information(this, QStringLiteral("Export"),
                                 QStringLiteral("This file does not contain images."));
        return;
    }

    ExportDialog dialog(m_plugin.get(), m_currentFile, this);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("About Gold Box Explorer"),
        QStringLiteral(
            "<h3>Gold Box Explorer %1</h3>"
            "<p>A cross-platform Qt port of the Gold Box Explorer by Bil Simser.</p>"
            "<p>Viewer and exporter for SSI Gold Box game files: DAX, TLB, GEO, ECL, "
            "FRUA data files, and more.</p>"
            "<p>Built with Qt %2.</p>")
            .arg(qApp->applicationVersion(), QString::fromLatin1(qVersion())));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel && watched != m_directoryEdit) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            if (wheel->angleDelta().y() > 0)
                zoomIn();
            else if (wheel->angleDelta().y() < 0)
                zoomOut();
            return true;
        }
    }

    if (watched == m_directoryEdit && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            selectRootFolder();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateViewerWidth();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_settings.setValue(QStringLiteral("rootFolder"), m_rootFolder);
    QMainWindow::closeEvent(event);
}
