#include "plugins/plugins.h"

#include "parse/audio.h"
#include "parse/iff.h"
#include "widgets/audioview.h"
#include "widgets/hexview.h"
#include "widgets/imagesheetview.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTabWidget>
#include <QTextStream>

namespace {

QString cleanText(const QByteArray &data)
{
    int end = data.indexOf('\0');
    if (end < 0)
        end = data.size();
    return QString::fromLatin1(data.left(end)).trimmed();
}

QStringList textList(const QByteArray &data)
{
    QStringList result;
    const QList<QByteArray> parts = data.split('\0');
    for (const QByteArray &part : parts) {
        const QString text = QString::fromLatin1(part).trimmed();
        if (!text.isEmpty())
            result.append(text);
    }
    return result;
}

QByteArray chunkData(const IffForm &form, const QByteArray &id)
{
    for (const IffChunk &chunk : form.chunks) {
        if (chunk.id == id)
            return chunk.data;
    }
    return {};
}

QString formatBytes(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ').toUpper());
}

QWidget *makeSummary(const QString &text)
{
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(text);
    return view;
}

QString describeInstrument(const IffForm &form, const QFileInfo &source, QVector<QString> *sampleNames)
{
    QString text;
    text += QStringLiteral("IFF FORM: INST (instrument / sound definition)\n");
    text += QStringLiteral("File: %1\n\n").arg(source.fileName());

    const QByteArray name = chunkData(form, QByteArrayLiteral("NAME"));
    if (!name.isEmpty())
        text += QStringLiteral("Name: %1\n\n").arg(cleanText(name));

    for (const char *id : {"PTCH", "VLUM", "SAMP"}) {
        const QByteArray data = chunkData(form, QByteArray(id));
        if (data.isEmpty())
            continue;
        const QStringList values = textList(data);
        if (values.isEmpty())
            continue;

        const QString label = QString::fromLatin1(id);
        if (label == QStringLiteral("PTCH"))
            text += QStringLiteral("Pitch reference: %1\n").arg(values.join(QStringLiteral(", ")));
        else if (label == QStringLiteral("VLUM"))
            text += QStringLiteral("Volume reference: %1\n").arg(values.join(QStringLiteral(", ")));
        else {
            text += QStringLiteral("Sample reference(s): %1\n").arg(values.join(QStringLiteral(", ")));
            if (sampleNames)
                *sampleNames = values;
        }
    }

    text += QStringLiteral("\nChunks:\n");
    for (const IffChunk &chunk : form.chunks) {
        text += QStringLiteral("  %1  %2 bytes")
                    .arg(QString::fromLatin1(chunk.id))
                    .arg(chunk.data.size());
        if (chunk.data.size() <= 32) {
            const QString value = cleanText(chunk.data);
            if (!value.isEmpty())
                text += QStringLiteral("  \"%1\"").arg(value);
        }
        text += QLatin1Char('\n');
    }
    return text;
}

QString describeMusic(const IffForm &form, const QFileInfo &source)
{
    QString text;
    text += QStringLiteral("IFF FORM: SMUS (Simple Musical Score)\n");
    text += QStringLiteral("File: %1\n\n").arg(source.fileName());

    const QByteArray name = chunkData(form, QByteArrayLiteral("NAME"));
    if (!name.isEmpty())
        text += QStringLiteral("Name: %1\n\n").arg(cleanText(name));

    const QByteArray shdr = chunkData(form, QByteArrayLiteral("SHDR"));
    if (!shdr.isEmpty())
        text += QStringLiteral("Score header: %1\n\n").arg(formatBytes(shdr));

    int index = 0;
    for (const IffChunk &chunk : form.chunks) {
        if (chunk.id == QByteArrayLiteral("INS1")) {
            QString instrumentName;
            if (chunk.data.size() >= 8)
                instrumentName = cleanText(chunk.data.mid(4));
            text += QStringLiteral("Instrument %1: %2\n")
                        .arg(index++)
                        .arg(instrumentName.isEmpty() ? QStringLiteral("<unnamed>") : instrumentName);
        }
    }

    const QByteArray trak = chunkData(form, QByteArrayLiteral("TRAK"));
    if (!trak.isEmpty()) {
        text += QStringLiteral("\nTRAK chunk: %1 bytes\n").arg(trak.size());
        text += QStringLiteral("Events (first bytes): %1\n").arg(formatBytes(trak.left(64)));
    }

    text += QStringLiteral("\nChunks:\n");
    for (const IffChunk &chunk : form.chunks)
        text += QStringLiteral("  %1  %2 bytes\n")
                    .arg(QString::fromLatin1(chunk.id))
                    .arg(chunk.data.size());
    return text;
}

QString describeGeneric(const IffForm &form, const QFileInfo &source)
{
    QString text;
    text += QStringLiteral("IFF FORM: %1\n").arg(QString::fromLatin1(form.type));
    text += QStringLiteral("File: %1\n\n").arg(source.fileName());
    text += QStringLiteral("Chunks:\n");
    for (const IffChunk &chunk : form.chunks) {
        text += QStringLiteral("  %1  %2 bytes").arg(QString::fromLatin1(chunk.id)).arg(chunk.data.size());
        if (chunk.data.size() <= 64) {
            const QString value = cleanText(chunk.data);
            if (!value.isEmpty())
                text += QStringLiteral("  \"%1\"").arg(value);
        }
        text += QLatin1Char('\n');
    }
    return text;
}

} // namespace

bool IffPlugin::isSatisfiedBy(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(12);
    return looksLikeIff(header);
}

QWidget *IffPlugin::createViewer(const PluginParameters &parameters)
{
    m_images.clear();
    m_isImage = false;

    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return makeSummary(QStringLiteral("Unable to open the IFF file."));
    const QByteArray data = file.readAll();
    const QFileInfo info(parameters.filename);

    IffForm form;
    QString error;
    if (!readIffForm(data, form, &error))
        return makeSummary(QStringLiteral("Unable to read IFF file:\n%1").arg(error));

    if (form.type == QByteArrayLiteral("8SVX") || form.type == QByteArrayLiteral("16SV")) {
        AudioSample sample;
        if (load8svx(data, sample, &error))
            return new AudioView(sample);
        return makeSummary(QStringLiteral("Unable to decode %1 audio:\n%2")
                               .arg(QString::fromLatin1(form.type), error));
    }

    if (form.type == QByteArrayLiteral("ILBM") || form.type == QByteArrayLiteral("PBM ")
        || form.type == QByteArrayLiteral("ANIM")) {
        const QImage image = decodeIffImage(data, &error);
        if (!image.isNull()) {
            m_images = {image};
            m_isImage = true;
            ImageSheetView::Options options;
            options.displayBorder = false;
            return new ImageSheetView(m_images, {}, options);
        }
    }

    auto *tabs = new QTabWidget;

    QVector<QString> sampleNames;
    QString summary;
    if (form.type == QByteArrayLiteral("INST"))
        summary = describeInstrument(form, info, &sampleNames);
    else if (form.type == QByteArrayLiteral("SMUS"))
        summary = describeMusic(form, info);
    else
        summary = describeGeneric(form, info);

    tabs->addTab(makeSummary(summary), QStringLiteral("Summary"));

    // For instrument definitions, resolve referenced sample names in the same
    // directory and expose any 8SVX sample as an audio tab.
    const QDir dir = info.absoluteDir();
    for (const QString &sampleName : sampleNames) {
        const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        QString match;
        for (const QString &entry : entries) {
            if (entry.compare(sampleName, Qt::CaseInsensitive) == 0
                || entry.compare(sampleName + QStringLiteral(".8svx"), Qt::CaseInsensitive) == 0
                || entry.compare(sampleName + QStringLiteral(".svx"), Qt::CaseInsensitive) == 0) {
                match = entry;
                break;
            }
        }
        if (match.isEmpty())
            continue;

        QFile sampleFile(dir.filePath(match));
        if (!sampleFile.open(QIODevice::ReadOnly))
            continue;
        AudioSample sample;
        QString audioError;
        if (load8svx(sampleFile.readAll(), sample, &audioError))
            tabs->addTab(new AudioView(sample), QStringLiteral("Sample: %1").arg(sampleName));
    }

    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

QVector<QImage> IffPlugin::getBitmaps()
{
    return m_images;
}
