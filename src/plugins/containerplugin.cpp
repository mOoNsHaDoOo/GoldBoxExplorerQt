#include "plugins/containerplugin.h"

#include "plugins/pluginfactory.h"
#include "plugins/plugins.h"
#include "widgets/hexview.h"
#include "widgets/imagesheetview.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <typeinfo>

namespace {

QPlainTextEdit *textView(const QString &text)
{
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(text);
    return view;
}

QTableWidgetItem *cell(const QString &text, bool right = false)
{
    auto *item = new QTableWidgetItem(text);
    if (right)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QString hexOffset(qint64 offset)
{
    if (offset < 0)
        return QStringLiteral("-");
    return QStringLiteral("0x%1").arg(offset, 0, 16).toUpper().replace(QStringLiteral("0X"),
                                                                       QStringLiteral("0x"));
}

} // namespace

ContainerPlugin::ContainerPlugin(Container container)
    : m_container(std::move(container))
    , m_guard(std::make_unique<QObject>())
{
}

ContainerPlugin::~ContainerPlugin()
{
    // Drop the signal connections before the plugins they refer to.
    m_guard.reset();
    m_entryPlugin.reset();
    m_wholePlugin.reset();
}

bool ContainerPlugin::isSatisfiedBy(const QString &path) const
{
    Container probe;
    return probe.open(path);
}

QWidget *ContainerPlugin::createViewer(const PluginParameters &parameters)
{
    m_parameters = parameters;
    m_zoom = parameters.zoom;
    m_containerWidth = parameters.containerWidth;

    auto *splitter = new QSplitter(Qt::Horizontal);

    // Left: directory of the container.
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("<b>%1</b><br>%2<br>%3")
                                 .arg(QFileInfo(m_container.path()).fileName().toHtmlEscaped(),
                                      m_container.formatName().toHtmlEscaped(),
                                      m_container.summary().toHtmlEscaped()));
    title->setWordWrap(true);
    title->setContentsMargins(4, 4, 4, 4);
    leftLayout->addWidget(title);

    auto *table = new QTableWidget;
    m_table = table;
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Id"),
                                      QStringLiteral("Offset"), QStringLiteral("Stored"),
                                      QStringLiteral("Size"), QStringLiteral("Type")});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->horizontalHeader()->setStretchLastSection(true);

    const QVector<ContainerEntry> &entries = m_container.entries();
    table->setRowCount(entries.size() + 1);
    table->setItem(0, 0, cell(QStringLiteral("*")));
    table->setItem(0, 1, cell(QString()));
    table->setItem(0, 2, cell(QString()));
    table->setItem(0, 3, cell(QString()));
    table->setItem(0, 4, cell(QString::number(QFileInfo(m_container.path()).size()), true));
    table->setItem(0, 5, cell(QStringLiteral("All blocks (file view)")));
    for (int i = 0; i < entries.size(); ++i) {
        const ContainerEntry &e = entries.at(i);
        const int row = i + 1;
        table->setItem(row, 0, cell(QString::number(e.index), true));
        table->setItem(row, 1, cell(QString::number(e.id), true));
        table->setItem(row, 2, cell(hexOffset(e.offset), true));
        table->setItem(row, 3, cell(e.offset < 0 ? QStringLiteral("-") : QString::number(e.storedSize), true));
        table->setItem(row, 4, cell(e.offset < 0 ? QStringLiteral("-") : QString::number(e.data.size()), true));
        table->setItem(row, 5, cell(e.type));
    }
    table->resizeColumnsToContents();
    leftLayout->addWidget(table, 1);

    auto *buttons = new QHBoxLayout;
    auto *exportDecoded = new QPushButton(QStringLiteral("Export block..."));
    exportDecoded->setToolTip(QStringLiteral("Save the decoded bytes of the selected block"));
    auto *exportStored = new QPushButton(QStringLiteral("Export stored..."));
    exportStored->setToolTip(QStringLiteral("Save the bytes of the selected block as stored in the file"));
    buttons->addWidget(exportDecoded);
    buttons->addWidget(exportStored);
    leftLayout->addLayout(buttons);

    // Right: the active viewer.
    auto *right = new QWidget;
    m_right = right;
    m_rightLayout = new QVBoxLayout(right);
    m_rightLayout->setContentsMargins(0, 0, 0, 0);

    m_wholePlugin = PluginFactory::createForFile(m_container.path());
    QWidget *whole = nullptr;
    if (m_wholePlugin) {
        PluginParameters wholeParameters = parameters;
        wholeParameters.containerWidth = viewerWidth();
        whole = m_wholePlugin->createViewer(wholeParameters);
    }
    if (!whole)
        whole = new QLabel(QStringLiteral("No viewer for this file."));
    m_wholeViewer = whole;
    m_rightLayout->addWidget(whole);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, qMax(200, parameters.containerWidth - 300)});

    QObject *guard = m_guard.get();
    QObject::connect(table, &QTableWidget::currentCellChanged, guard,
                     [this](int row, int, int, int) {
                         if (row >= 0 && row != m_currentRow)
                             showRow(row);
                     });
    QObject::connect(splitter, &QSplitter::splitterMoved, guard,
                     [this] { setContainerWidth(m_containerWidth); });
    auto selectedEntry = [this]() -> int {
        return m_table ? m_table->currentRow() - 1 : -1;
    };
    QObject::connect(exportDecoded, &QPushButton::clicked, guard,
                     [this, selectedEntry] { exportEntry(selectedEntry(), true); });
    QObject::connect(exportStored, &QPushButton::clicked, guard,
                     [this, selectedEntry] { exportEntry(selectedEntry(), false); });
    QObject::connect(table, &QWidget::customContextMenuRequested, guard,
                     [this, table](const QPoint &pos) {
                         const int row = table->rowAt(pos.y());
                         if (row <= 0)
                             return;
                         QMenu menu(table);
                         QAction *decoded = menu.addAction(QStringLiteral("Export decoded block..."));
                         QAction *stored = menu.addAction(QStringLiteral("Export stored bytes..."));
                         const ContainerEntry &e = m_container.entries().at(row - 1);
                         stored->setEnabled(e.offset >= 0);
                         decoded->setEnabled(e.offset >= 0);
                         QAction *chosen = menu.exec(table->viewport()->mapToGlobal(pos));
                         if (chosen == decoded)
                             exportEntry(row - 1, true);
                         else if (chosen == stored)
                             exportEntry(row - 1, false);
                     });

    {
        const QSignalBlocker blocker(table);
        table->setCurrentCell(0, 0);
    }
    m_currentRow = 0;
    return splitter;
}

void ContainerPlugin::selectRow(int row)
{
    if (m_table)
        m_table->setCurrentCell(row, 0);
    else
        showRow(row);
}

void ContainerPlugin::showRow(int row)
{
    m_currentRow = row;

    // Drop the previous block viewer and its plugin.
    if (m_entryViewer) {
        m_rightLayout->removeWidget(m_entryViewer);
        delete m_entryViewer;
    }
    m_entryPlugin.reset();
    m_entryImages.clear();
    m_entryViewerName.clear();

    if (row <= 0) {
        if (m_wholeViewer)
            m_wholeViewer->show();
        setContainerWidth(m_containerWidth);
        return;
    }

    if (m_wholeViewer)
        m_wholeViewer->hide();
    QWidget *viewer = createEntryViewer(row - 1);
    m_entryViewer = viewer;
    m_rightLayout->addWidget(viewer);
    setContainerWidth(m_containerWidth);
}

QString ContainerPlugin::writeSingleEntryFile(int index)
{
    const QByteArray bytes = m_container.singleEntryFile(index);
    if (bytes.isEmpty())
        return {};

    const QFileInfo info(m_container.path());
    if (!m_tempDir) {
        m_tempDir = std::make_unique<QTemporaryDir>();
        if (!m_tempDir->isValid())
            return {};
    }
    // Mirror the absolute folder so path-based heuristics (game detection,
    // companion files such as WALLDEF/8X8D/ECL next to GEO) still apply.
    QString folder = info.absolutePath();
    folder.replace(QLatin1Char(':'), QLatin1Char('_'));
    const QString mirror = QDir(m_tempDir->path()).filePath(folder.mid(folder.startsWith(QLatin1Char('/')) ? 1 : 0));
    QDir mirrorDir(mirror);
    if (!mirrorDir.exists()) {
        if (!QDir().mkpath(mirror))
            return {};
        const QFileInfoList siblings = info.absoluteDir().entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &sibling : siblings) {
            if (sibling.fileName() == info.fileName())
                continue;
            QFile::link(sibling.absoluteFilePath(), mirrorDir.filePath(sibling.fileName()));
        }
    }

    const QString target = mirrorDir.filePath(info.fileName());
    QFile::remove(target);
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
        return {};
    return target;
}

QWidget *ContainerPlugin::createEntryViewer(int index)
{
    const ContainerEntry &e = m_container.entries().at(index);
    auto *tabs = new QTabWidget;

    QString viewNote;
    QWidget *view = nullptr;
    if (m_container.canExtractSingleEntry() && e.decoded) {
        const QString path = writeSingleEntryFile(index);
        if (!path.isEmpty()) {
            auto plugin = PluginFactory::createForFile(path);
            if (plugin && !dynamic_cast<HexPlugin *>(plugin.get())) {
                PluginParameters parameters = m_parameters;
                parameters.filename = path;
                parameters.zoom = m_zoom;
                parameters.containerWidth = viewerWidth();
                view = plugin->createViewer(parameters);
                if (view) {
                    const Plugin &ref = *plugin;
                    m_entryViewerName = QString::fromLatin1(typeid(ref).name())
                                            .remove(QRegularExpression(QStringLiteral("^[0-9]+")));
                    m_entryPlugin = std::move(plugin);
                }
            } else {
                viewNote = QStringLiteral("No specific viewer for this block type: showing the hex dump.");
            }
        }
    }
    if (!view && !e.images.isEmpty()) {
        m_entryImages = e.images;
        auto *sheet = new ImageSheetView(e.images, {}, {});
        sheet->setZoom(m_zoom);
        sheet->setContainerWidth(viewerWidth());
        m_entrySheet = sheet;
        m_entryViewerName = QStringLiteral("images");
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        scroll->setWidget(sheet);
        view = scroll;
    }
    if (view)
        tabs->addTab(view, QStringLiteral("View"));

    QString info;
    info += QStringLiteral("Container: %1\n").arg(QFileInfo(m_container.path()).fileName());
    info += QStringLiteral("Format:    %1\n\n").arg(m_container.formatName());
    info += QStringLiteral("Entry:     %1\n").arg(e.index);
    info += QStringLiteral("Id:        %1 (0x%2)\n").arg(e.id).arg(e.id, 2, 16, QLatin1Char('0'));
    if (e.offset >= 0) {
        info += QStringLiteral("Offset:    %1 (%2)\n").arg(e.offset).arg(hexOffset(e.offset));
        info += QStringLiteral("Stored:    %1 bytes\n").arg(e.storedSize);
        if (e.declaredSize > 0)
            info += QStringLiteral("Declared:  %1 bytes\n").arg(e.declaredSize);
        info += QStringLiteral("Decoded:   %1 bytes%2\n")
                    .arg(e.data.size())
                    .arg(e.decoded ? QString() : QStringLiteral(" (decompression failed)"));
    }
    info += QStringLiteral("Type:      %1\n\n").arg(e.type);
    info += e.details;
    if (!viewNote.isEmpty())
        info += QStringLiteral("\n\n") + viewNote;
    tabs->addTab(textView(info), QStringLiteral("Info"));

    if (e.offset >= 0) {
        tabs->addTab(new HexView(e.data), QStringLiteral("Hex (decoded)"));
        if (e.stored != e.data)
            tabs->addTab(new HexView(e.stored), QStringLiteral("Hex (stored)"));
    }
    if (!view && e.offset >= 0)
        tabs->setCurrentIndex(1);
    return tabs;
}

void ContainerPlugin::exportEntry(int index, bool decoded)
{
    if (index < 0 || index >= m_container.entries().size())
        return;
    const ContainerEntry &e = m_container.entries().at(index);
    if (e.offset < 0)
        return;
    const QFileInfo info(m_container.path());
    const QString suggested = QDir::home().filePath(
        QStringLiteral("%1_%2%3.bin")
            .arg(info.completeBaseName())
            .arg(e.id)
            .arg(decoded ? QString() : QStringLiteral("_stored")));
    const QString target = QFileDialog::getSaveFileName(
        m_table, decoded ? QStringLiteral("Export decoded block") : QStringLiteral("Export stored block"),
        suggested);
    if (target.isEmpty())
        return;
    QFile out(target);
    const QByteArray &bytes = decoded ? e.data : e.stored;
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
        QMessageBox::warning(m_table, QStringLiteral("Export"),
                             QStringLiteral("Unable to write %1").arg(target));
}

int ContainerPlugin::viewerWidth() const
{
    if (m_right && m_right->width() > 50)
        return m_right->width() - 20;
    return qMax(1, m_containerWidth - 320);
}

void ContainerPlugin::setZoom(float zoom)
{
    m_zoom = zoom;
    if (m_wholePlugin)
        m_wholePlugin->setZoom(zoom);
    if (m_entryPlugin)
        m_entryPlugin->setZoom(zoom);
    if (m_entrySheet)
        m_entrySheet->setZoom(zoom);
}

void ContainerPlugin::setContainerWidth(int width)
{
    m_containerWidth = width;
    const int inner = viewerWidth();
    if (m_wholePlugin)
        m_wholePlugin->setContainerWidth(inner);
    if (m_entryPlugin)
        m_entryPlugin->setContainerWidth(inner);
    if (m_entrySheet)
        m_entrySheet->setContainerWidth(inner);
}

bool ContainerPlugin::isImageFile() const
{
    if (m_currentRow <= 0)
        return m_wholePlugin && m_wholePlugin->isImageFile();
    if (m_entryPlugin)
        return m_entryPlugin->isImageFile();
    return !m_entryImages.isEmpty();
}

QVector<QImage> ContainerPlugin::getBitmaps()
{
    if (m_currentRow <= 0)
        return m_wholePlugin ? m_wholePlugin->getBitmaps() : QVector<QImage>{};
    if (m_entryPlugin)
        return m_entryPlugin->getBitmaps();
    return m_entryImages;
}

QVector<int> ContainerPlugin::getBitmapIds()
{
    if (m_currentRow <= 0)
        return m_wholePlugin ? m_wholePlugin->getBitmapIds() : QVector<int>{};
    if (m_entryPlugin)
        return m_entryPlugin->getBitmapIds();
    return {};
}
