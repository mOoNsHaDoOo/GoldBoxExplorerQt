#include "plugins/plugins.h"

#include "core/byteutils.h"
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
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTableWidget>
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
            QString header;
            if (block.data.size() >= 9) {
                header = QStringLiteral("  ")
                         + inspectDaaRecord(block.data, isEightByEightDaaName(daa->path()))
                               .description;
            }
            text += QStringLiteral("%1  %2  %3  %4  %5%6\n")
                        .arg(block.id, 3)
                        .arg(block.offset, 8)
                        .arg(block.rawSize, 6)
                        .arg(block.compressedSize, 6)
                        .arg(block.data.size(), 7)
                        .arg(header);
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

    // Picture sequences used by pic*.daa / SPRIT*.DAA (Load_Sequence).
    DaaSequenceFile sequences;
    if (sequences.parse(data)) {
        for (const DaaSequence &seq : sequences.sequences()) {
            for (const QImage &frame : seq.frames) {
                m_daaImages.append(frame);
                m_daaIds.append(seq.id);
            }
        }
        if (!m_daaImages.isEmpty()) {
            m_isImage = true;
            ImageSheetView::Options options;
            options.displayBorder = true;
            m_sheet = new ImageSheetView(m_daaImages, m_daaIds, options);
            m_sheet->setContainerWidth(parameters.containerWidth);
            m_sheet->setZoom(parameters.zoom);

            QString summary;
            summary += QStringLiteral("Amiga picture sequence container (Load_Sequence)\n");
            summary += QStringLiteral("Directory size: %1 bytes, %2 sequences\n\n")
                           .arg(sequences.dataOffset())
                           .arg(sequences.sequences().size());
            summary += QStringLiteral(" id  offset  stored  size     frames  durations (1/10 s)\n");
            for (const DaaSequence &seq : sequences.sequences()) {
                QStringList durations;
                for (int d : seq.durations)
                    durations.append(QString::number(d));
                summary += QStringLiteral("%1  %2  %3  %4x%5  %6       %7%8\n")
                               .arg(seq.id, 3)
                               .arg(seq.offset, 6)
                               .arg(seq.storedSize, 6)
                               .arg(seq.units * 8, 3)
                               .arg(seq.rows, -3)
                               .arg(seq.frameCount)
                               .arg(durations.join(QLatin1Char(' ')))
                               .arg(seq.error.isEmpty() ? QString()
                                                        : QStringLiteral("  [%1]").arg(seq.error));
            }
            summary += QStringLiteral("\nFrames after the first are stored XOR-ed with frame 1;\n"
                                      "they are shown here already combined. The game mirrors\n"
                                      "pictures at random and shows only frame 1 of PIC/FINAL\n"
                                      "sequences when animation is off.\n");

            auto *tabs = new QTabWidget;
            tabs->addTab(scrollFor(m_sheet), QStringLiteral("Frames"));
            tabs->addTab(summaryView(summary), QStringLiteral("Summary"));
            tabs->addTab(new HexView(data), QStringLiteral("Hex"));
            return tabs;
        }
    }

    // Mouse pointers (pointers.daa).
    if (QFileInfo(parameters.filename).fileName().startsWith(QStringLiteral("pointers"),
                                                             Qt::CaseInsensitive)) {
        QVector<DaaPointer> pointers;
        if (decodePointerFile(data, pointers)) {
            QString summary = QStringLiteral("Amiga mouse pointers (SetPointer data, 16 pixels wide)\n\n"
                                             " #  offset  hotX  hotY  height\n");
            for (const DaaPointer &p : pointers) {
                m_daaImages.append(p.image);
                m_daaIds.append(p.index);
                summary += QStringLiteral("%1  %2  %3  %4  %5\n")
                               .arg(p.index, 2).arg(p.offset, 6).arg(p.hotX, 4)
                               .arg(p.hotY, 4).arg(p.height, 6);
            }
            summary += QStringLiteral("\nThe pointer colours (sprite colours 17-19) come from the\n"
                                      "screen palette at run time; placeholder colours are used.\n");
            m_isImage = true;
            m_sheet = new ImageSheetView(m_daaImages, m_daaIds, {});
            m_sheet->setContainerWidth(parameters.containerWidth);
            m_sheet->setZoom(parameters.zoom);
            auto *tabs = new QTabWidget;
            tabs->addTab(scrollFor(m_sheet), QStringLiteral("Pointers"));
            tabs->addTab(summaryView(summary), QStringLiteral("Summary"));
            tabs->addTab(new HexView(data), QStringLiteral("Hex"));
            return tabs;
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
