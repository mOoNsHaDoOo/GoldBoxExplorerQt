#include "plugins/plugins.h"

#include "widgets/hexview.h"

#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {

constexpr int kFirst = 32;
constexpr int kGlyphs = 96;

bool glyphPixel(const QByteArray &font, int glyph, int x, int y)
{
    const int at = glyph * 8 + y;
    return at < font.size() && ((static_cast<quint8>(font.at(at)) >> (7 - x)) & 1);
}

void drawGlyph(QImage &image, const QByteArray &font, int glyph, int ox, int oy, QRgb ink)
{
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (glyphPixel(font, glyph, x, y) && image.rect().contains(ox + x, oy + y))
                image.setPixel(ox + x, oy + y, ink);
}

// All characters in a 16 x 6 grid with their codes.
QImage characterSheet(const QByteArray &font, int scale)
{
    const int cell = 8 * scale + 24;
    QImage sheet(16 * cell, 6 * (cell + 12), QImage::Format_RGB32);
    sheet.fill(QColor(24, 24, 30));
    QPainter p(&sheet);
    p.setFont(QFont(QStringLiteral("Courier New"), 8));
    for (int g = 0; g < kGlyphs; ++g) {
        QImage glyph(8, 8, QImage::Format_RGB32);
        glyph.fill(QColor(0, 0, 0));
        drawGlyph(glyph, font, g, 0, 0, qRgb(240, 240, 220));
        const int cx = (g % 16) * cell + 12;
        const int cy = (g / 16) * (cell + 12) + 8;
        p.drawImage(QRect(cx, cy, 8 * scale, 8 * scale), glyph);
        p.setPen(QColor(120, 140, 160));
        p.drawText(QRect(cx - 12, cy + 8 * scale + 2, 8 * scale + 24, 14), Qt::AlignCenter,
                   QStringLiteral("%1 $%2").arg(QChar(kFirst + g)).arg(kFirst + g, 2, 16, QLatin1Char('0')));
    }
    return sheet;
}

// A line of text written with the font (characters outside 32..127 blank).
QImage textImage(const QByteArray &font, const QString &text)
{
    QImage image(qMax(1, text.size()) * 8, 8, QImage::Format_RGB32);
    image.fill(QColor(0, 0, 0));
    for (int i = 0; i < text.size(); ++i) {
        const int code = text.at(i).unicode();
        if (code >= kFirst && code < kFirst + kGlyphs)
            drawGlyph(image, font, code - kFirst, i * 8, 0, qRgb(240, 240, 220));
    }
    return image;
}

} // namespace

bool GoldBoxFontPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    return info.suffix().compare(QStringLiteral("fon"), Qt::CaseInsensitive) == 0 && info.size() == kGlyphs * 8;
}

QWidget *GoldBoxFontPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return new QLabel(QStringLiteral("Unable to open the file."));
    const QByteArray font = file.readAll();

    auto *tabs = new QTabWidget;

    // the character set, scaled
    auto *sheetPage = new QWidget;
    auto *sheetLayout = new QVBoxLayout(sheetPage);
    auto *scaleRow = new QHBoxLayout;
    auto *scale = new QSpinBox;
    scale->setRange(1, 8);
    scale->setValue(4);
    scale->setPrefix(QStringLiteral("Zoom ×"));
    scaleRow->addWidget(new QLabel(QStringLiteral("8×8 font, 96 characters from $20 (space) to $7F")));
    scaleRow->addStretch(1);
    scaleRow->addWidget(scale);
    sheetLayout->addLayout(scaleRow);
    auto *sheetLabel = new QLabel;
    auto *sheetScroll = new QScrollArea;
    sheetScroll->setWidget(sheetLabel);
    sheetLayout->addWidget(sheetScroll, 1);
    auto updateSheet = [sheetLabel, font](int s) {
        sheetLabel->setPixmap(QPixmap::fromImage(characterSheet(font, s)));
        sheetLabel->resize(sheetLabel->pixmap().size());
    };
    QObject::connect(scale, &QSpinBox::valueChanged, sheetLabel, updateSheet);
    updateSheet(scale->value());
    tabs->addTab(sheetPage, QStringLiteral("Characters"));

    // free text written with the font
    auto *textPage = new QWidget;
    auto *textLayout = new QVBoxLayout(textPage);
    auto *edit = new QLineEdit(QStringLiteral("You have entered the Gold Box. 0123456789 !?"));
    textLayout->addWidget(edit);
    auto *textLabel = new QLabel;
    textLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto *textScroll = new QScrollArea;
    textScroll->setWidget(textLabel);
    textLayout->addWidget(textScroll, 1);
    auto updateText = [textLabel, font](const QString &text) {
        const QImage line = textImage(font, text);
        textLabel->setPixmap(QPixmap::fromImage(line.scaled(line.width() * 3, line.height() * 3)));
        textLabel->resize(textLabel->pixmap().size());
    };
    QObject::connect(edit, &QLineEdit::textChanged, textLabel, updateText);
    updateText(edit->text());
    tabs->addTab(textPage, QStringLiteral("Text preview"));

    tabs->addTab(new HexView(font), QStringLiteral("Hex"));
    return tabs;
}
