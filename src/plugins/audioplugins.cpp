#include "plugins/plugins.h"

#include "core/daxfile.h"
#include "dax/daximagefile.h"
#include "parse/audio.h"
#include "parse/iff.h"
#include "widgets/audioview.h"
#include "widgets/hexview.h"
#include "widgets/imagesheetview.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QStringList>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTabWidget>

#include <utility>

namespace {

QWidget *scrollFor(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scroll->setWidget(content);
    return scroll;
}

QString printableStrings(const QByteArray &data)
{
    QStringList strings;
    QByteArray current;
    for (char c : data) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 32 && u <= 126) {
            current.append(c);
        } else {
            if (current.size() >= 4)
                strings.append(QString::fromLatin1(current));
            current.clear();
        }
        if (strings.size() >= 80)
            break;
    }
    if (current.size() >= 4)
        strings.append(QString::fromLatin1(current));
    return strings.join(QLatin1Char('\n'));
}

QPlainTextEdit *summaryView(const QString &text)
{
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(text);
    return view;
}

QString describeDaa(const QByteArray &data, const DaaFile *daa = nullptr)
{
    QString text;
    text += QStringLiteral("File size: %1 bytes\n").arg(data.size());
    text += QStringLiteral("Magic: %1\n\n")
                .arg(QString::fromLatin1(data.left(16).toHex(' ').toUpper()));

    if (daa && daa->isValid()) {
        text += QStringLiteral("DAA container: data offset %1, %2 blocks\n\n")
                    .arg(daa->dataOffset())
                    .arg(daa->blocks().size());
        text += QStringLiteral(" id   offset      raw    comp   decoded\n");
        for (const DaaBlock &block : daa->blocks()) {
            text += QStringLiteral("%3  %8  %6  %6  %7\n")
                        .arg(block.id)
                        .arg(block.offset)
                        .arg(block.rawSize)
                        .arg(block.compressedSize)
                        .arg(block.data.size());
        }
        text += QLatin1Char('\n');
    }

    IffForm form;
    if (readIffForm(data, form)) {
        text += QStringLiteral("IFF FORM type: %1\n").arg(QString::fromLatin1(form.type));
        text += QStringLiteral("Chunks:\n");
        for (const IffChunk &chunk : form.chunks) {
            text += QStringLiteral("  %1  %2 bytes\n")
                        .arg(QString::fromLatin1(chunk.id))
                        .arg(chunk.data.size());
        }
        text += QLatin1Char('\n');
    }

    const QString strings = printableStrings(data);
    if (!strings.isEmpty()) {
        text += QStringLiteral("Printable strings:\n");
        text += strings;
        text += QLatin1Char('\n');
    }
    return text;
}

} // namespace

bool AudioPlugin::isSatisfiedBy(const QString &path) const
{
    // Amiga Gold Box sound files usually have no extension, so detect the
    // IFF form instead of relying on the file name.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return looksLike8svx(file.read(12));
}

QWidget *AudioPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return summaryView(QStringLiteral("Unable to open the audio file."));

    AudioSample sample;
    QString error;
    if (!load8svx(file.readAll(), sample, &error))
        return summaryView(QStringLiteral("Unable to decode 8SVX audio:\n%1").arg(error));

    return new AudioView(sample);
}

bool DaaPlugin::isSatisfiedBy(const QString &path) const
{
    const QString suffix = QFileInfo(path).suffix().toUpper();
    return suffix == QStringLiteral("DAA") || suffix == QStringLiteral("DAI")
           || suffix == QStringLiteral("ANIM") || suffix == QStringLiteral("IFF")
           || suffix == QStringLiteral("LBM") || suffix == QStringLiteral("ILBM");
}

QWidget *DaaPlugin::createViewer(const PluginParameters &parameters)
{
    m_image = {};
    m_dax.reset();
    m_daa.reset();
    m_daaImages.clear();
    m_daaIds.clear();
    m_sheet = nullptr;
    m_isImage = false;
    m_zoom = parameters.zoom;
    m_containerWidth = parameters.containerWidth;

    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return summaryView(QStringLiteral("Unable to open the file."));
    const QByteArray data = file.readAll();

    // The Amiga DAA container used by the Gold Box ports.
    DaaFile daa(parameters.filename);
    if (daa.isValid()) {
        QVector<int> ids;
        QString error;
        const QVector<QImage> images = decodeDaaImages(daa, &ids, &error);
        if (!images.isEmpty()) {
            m_daa = std::make_unique<DaaFile>(std::move(daa));
            m_daaImages = images;
            m_daaIds = ids;
            m_isImage = true;
            ImageSheetView::Options options;
            const QString name = QFileInfo(parameters.filename).fileName().toUpper();
            options.thirtyFivePerRow = name.contains(QStringLiteral("8X8D"));
            options.displayBorder = name.contains(QStringLiteral("SPRIT"));
            m_sheet = new ImageSheetView(images, ids, options);
            m_sheet->setContainerWidth(parameters.containerWidth);
            m_sheet->setZoom(parameters.zoom);
            return scrollFor(m_sheet);
        }
    }

    // Amiga IFF audio hidden behind a .DAA/.DAI extension.
    if (looksLike8svx(data)) {
        AudioSample sample;
        QString error;
        if (load8svx(data, sample, &error))
            return new AudioView(sample);
    }

    // IFF ILBM/PBM, and the base frame of an ANIM file.
    if (looksLikeIff(data, QByteArrayLiteral("ILBM"))
        || looksLikeIff(data, QByteArrayLiteral("PBM "))
        || looksLikeIff(data, QByteArrayLiteral("ANIM"))) {
        QString error;
        const QImage image = decodeIffImage(data, &error);
        if (!image.isNull()) {
            m_image = image;
            m_isImage = true;
            m_sheet = new ImageSheetView({image}, {}, {});
            m_sheet->setContainerWidth(parameters.containerWidth);
            m_sheet->setZoom(parameters.zoom);
            return scrollFor(m_sheet);
        }
    }

    // Some .DAA files are really DAX containers.
    DaxFile dax(parameters.filename);
    if (dax.isValid() && !dax.blocks().isEmpty()) {
        m_dax = std::make_unique<DaxImageFile>(parameters.filename);
        if (!m_dax->bitmaps().isEmpty()) {
            m_isImage = true;
            ImageSheetView::Options options;
            const QString name = QFileInfo(parameters.filename).fileName().toUpper();
            options.thirtyFivePerRow = name.startsWith(QStringLiteral("8X8D"));
            options.displayBorder = name.startsWith(QStringLiteral("SPRIT"));
            m_sheet = new ImageSheetView(m_dax->bitmaps(), m_dax->bitmapIds(), options);
            m_sheet->setContainerWidth(parameters.containerWidth);
            m_sheet->setZoom(parameters.zoom);
            return scrollFor(m_sheet);
        }
    }

    auto *tabs = new QTabWidget;
    tabs->addTab(summaryView(describeDaa(data, daa.isValid() ? &daa : nullptr)),
                 QStringLiteral("Summary"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

void DaaPlugin::setZoom(float zoom)
{
    m_zoom = zoom;
    if (m_sheet)
        m_sheet->setZoom(zoom);
}

void DaaPlugin::setContainerWidth(int width)
{
    m_containerWidth = width;
    if (m_sheet)
        m_sheet->setContainerWidth(width);
}

QVector<QImage> DaaPlugin::getBitmaps()
{
    if (!m_daaImages.isEmpty())
        return m_daaImages;
    if (m_dax)
        return m_dax->bitmaps();
    if (!m_image.isNull())
        return {m_image};
    return {};
}

QVector<int> DaaPlugin::getBitmapIds()
{
    if (!m_daaIds.isEmpty())
        return m_daaIds;
    if (m_dax)
        return m_dax->bitmapIds();
    return {};
}
