#include "widgets/walltemplateexportdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const int xOffsets[10] = {66, 131, 196, 261, 1, 66, 131, 196, 261, 1};
const int yOffsets[10] = {1, 1, 1, 1, 61, 61, 61, 61, 61, 1};
const QRgb comparisonColor = qRgb(103, 247, 159);

} // namespace

WallTemplateExportDialog::WallTemplateExportDialog(const QVector<QImage> &walls,
                                                   QWidget *parent)
    : QDialog(parent)
    , m_walls(walls)
{
    setWindowTitle(QStringLiteral("Export Wall Template"));
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);

    auto *templateRow = new QHBoxLayout;
    auto *templateButton = new QPushButton(QStringLiteral("Template image..."));
    m_templateLabel = new QLabel(QStringLiteral("(none: use a blank template)"));
    templateRow->addWidget(templateButton);
    templateRow->addWidget(m_templateLabel, 1);
    layout->addLayout(templateRow);

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("Wall set:")));
    m_wallCombo = new QComboBox;
    for (int i = 0; i + 10 <= m_walls.size(); i += 10)
        m_wallCombo->addItem(QStringLiteral("%1").arg(i / 10 + 1));
    controls->addWidget(m_wallCombo);

    m_overlay = new QCheckBox(QStringLiteral("Overlay differences with"));
    controls->addWidget(m_overlay);
    m_overlayCombo = new QComboBox;
    for (int i = 0; i + 10 <= m_walls.size(); i += 10)
        m_overlayCombo->addItem(QStringLiteral("%1").arg(i / 10 + 1));
    controls->addWidget(m_overlayCombo);
    controls->addStretch(1);
    layout->addLayout(controls);

    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(480, 320);
    m_preview->setStyleSheet(QStringLiteral("background: #202020; border: 1px solid #555;"));
    layout->addWidget(m_preview, 1);

    auto *buttons = new QHBoxLayout;
    auto *saveButton = new QPushButton(QStringLiteral("Save template..."));
    auto *closeButton = new QPushButton(QStringLiteral("Close"));
    buttons->addStretch(1);
    buttons->addWidget(saveButton);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(templateButton, &QPushButton::clicked, this, &WallTemplateExportDialog::chooseTemplate);
    connect(m_wallCombo, &QComboBox::currentIndexChanged, this, &WallTemplateExportDialog::updatePreview);
    connect(m_overlay, &QCheckBox::toggled, this, &WallTemplateExportDialog::updatePreview);
    connect(m_overlayCombo, &QComboBox::currentIndexChanged, this, &WallTemplateExportDialog::updatePreview);
    connect(saveButton, &QPushButton::clicked, this, &WallTemplateExportDialog::saveTemplate);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    updatePreview();
}

QImage WallTemplateExportDialog::templateImage() const
{
    return buildTemplate();
}

void WallTemplateExportDialog::chooseTemplate()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose wall template"),
        QString(), QStringLiteral("Images (*.png *.bmp *.jpg *.jpeg *.gif);;All files (*)"));
    if (path.isEmpty())
        return;
    QImage image(path);
    if (image.isNull())
        return;
    m_template = image.convertToFormat(QImage::Format_ARGB32);
    m_templateLabel->setText(QFileInfo(path).fileName());
    updatePreview();
}

QImage WallTemplateExportDialog::buildTemplate() const
{
    QImage base = m_template;
    if (base.isNull()) {
        base = QImage(330, 180, QImage::Format_ARGB32);
        base.fill(Qt::white);
    } else {
        base = base.convertToFormat(QImage::Format_ARGB32);
    }

    const int start = qMax(0, m_wallCombo->currentIndex() * 10);
    const int overlayStart = qMax(0, m_overlayCombo->currentIndex() * 10);
    const bool overlay = m_overlay->isChecked()
                         && overlayStart + 10 <= m_walls.size();

    QPainter painter(&base);
    for (int i = 0; i < 10; ++i) {
        if (start + i >= m_walls.size())
            break;
        QImage wall = m_walls.at(start + i).convertToFormat(QImage::Format_ARGB32);
        const int crop = (i == 7 || i == 8) ? 0 : 8;
        if (wall.height() > crop)
            wall = wall.copy(0, crop, wall.width(), wall.height() - crop);
        if (wall.isNull() || wall.width() <= 0 || wall.height() <= 0)
            continue;

        if (overlay && overlayStart + i < m_walls.size()) {
            QImage other = m_walls.at(overlayStart + i).convertToFormat(QImage::Format_ARGB32);
            if (other.height() > crop)
                other = other.copy(0, crop, other.width(), other.height() - crop);
            if (other.size() == wall.size()) {
                for (int y = 0; y < wall.height(); ++y) {
                    QRgb *line = reinterpret_cast<QRgb *>(wall.scanLine(y));
                    const QRgb *otherLine = reinterpret_cast<const QRgb *>(other.constScanLine(y));
                    for (int x = 0; x < wall.width(); ++x) {
                        if (line[x] == otherLine[x])
                            line[x] = comparisonColor;
                    }
                }
            }
        }
        painter.drawImage(QPoint(xOffsets[i], yOffsets[i]), wall);
    }
    painter.end();
    return base;
}

void WallTemplateExportDialog::updatePreview()
{
    const QImage image = buildTemplate();
    m_preview->setPixmap(QPixmap::fromImage(image).scaled(
        m_preview->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void WallTemplateExportDialog::saveTemplate()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save wall template"),
        QStringLiteral("wall_template.png"),
        QStringLiteral("PNG image (*.png);;BMP image (*.bmp);;JPEG image (*.jpg)"));
    if (path.isEmpty())
        return;
    buildTemplate().save(path);
}
