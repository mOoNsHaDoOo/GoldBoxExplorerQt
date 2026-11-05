#include "ExportDialog.h"

#include "plugins/plugins.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

ExportDialog::ExportDialog(Plugin *plugin, const QString &sourceFile, QWidget *parent)
    : QDialog(parent)
    , m_plugin(plugin)
    , m_sourceFile(sourceFile)
{
    setWindowTitle(QStringLiteral("Export Images"));

    m_folderEdit = new QLineEdit;
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty())
        pictures = QDir::homePath();
    m_folderEdit->setText(pictures);

    auto *browseButton = new QPushButton(QStringLiteral("Browse..."));
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);

    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(browseButton);

    m_formatCombo = new QComboBox;
    m_formatCombo->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_formatCombo->addItem(QStringLiteral("BMP"), QStringLiteral("bmp"));
    m_formatCombo->addItem(QStringLiteral("JPEG"), QStringLiteral("jpg"));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Target folder"), folderRow);
    form->addRow(QStringLiteral("Format"), m_formatCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Export"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::exportImages);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(new QLabel(QStringLiteral(
        "Images are written as <name>_<block>_<index>.<format>.")));
    layout->addWidget(buttons);
}

void ExportDialog::browse()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select target folder"), m_folderEdit->text());
    if (!folder.isEmpty())
        m_folderEdit->setText(folder);
}

void ExportDialog::exportImages()
{
    if (!m_plugin)
        return;

    const QString targetFolder = m_folderEdit->text();
    if (targetFolder.isEmpty() || !QDir(targetFolder).exists()) {
        QMessageBox::warning(this, QStringLiteral("Export"),
                             QStringLiteral("Please choose an existing target folder."));
        return;
    }

    const QVector<QImage> bitmaps = m_plugin->getBitmaps();
    if (bitmaps.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Export"),
                             QStringLiteral("This file does not contain any exportable images."));
        return;
    }

    const QVector<int> ids = m_plugin->getBitmapIds();
    const QString format = m_formatCombo->currentData().toString();

    QString base = QFileInfo(m_sourceFile).fileName();
    if (base.toUpper().endsWith(QStringLiteral(".DAX")))
        base.chop(4);

    int exported = 0;
    for (int i = 0; i < bitmaps.size(); ++i) {
        const QImage &image = bitmaps[i];
        if (image.isNull())
            continue;

        QImage canvas(image.size(), QImage::Format_ARGB32);
        canvas.fill(QColor(103, 247, 159));
        {
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(0, 0, image);
        }

        const int blockId = i < ids.size() ? ids[i] : 0;
        const QString output = QDir(targetFolder).filePath(
            QStringLiteral("%1_%2_%3.%4").arg(base).arg(blockId).arg(i).arg(format));

        if (canvas.save(output))
            ++exported;
    }

    QMessageBox::information(this, QStringLiteral("Export"),
                             QStringLiteral("Exported %1 images as %2 to %3")
                                 .arg(exported)
                                 .arg(format.toUpper(), targetFolder));
    accept();
}
