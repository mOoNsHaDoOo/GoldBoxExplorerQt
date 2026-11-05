#include "plugins/plugins.h"

#include "parse/audio.h"
#include "parse/character.h"
#include "parse/character_tables.h"
#include "parse/iff.h"
#include "widgets/audioview.h"
#include "widgets/characterview.h"
#include "widgets/hexview.h"

#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QtEndian>

namespace {

QString printableStrings(const QByteArray &data, int maxStrings = 200)
{
    QStringList result;
    QByteArray current;
    for (char c : data) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 32 && u <= 126) {
            current.append(c);
        } else {
            if (current.size() >= 4)
                result.append(QString::fromLatin1(current));
            current.clear();
        }
        if (result.size() >= maxStrings)
            break;
    }
    if (current.size() >= 4)
        result.append(QString::fromLatin1(current));
    return result.join(QLatin1Char('\n'));
}

QPlainTextEdit *textView(const QString &text)
{
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    view->setPlainText(text);
    return view;
}

QString recordText(const QByteArray &record, int maxBytes)
{
    QStringList parts;
    QByteArray current;
    const int limit = qMin(maxBytes, record.size());
    for (int i = 0; i < limit; ++i) {
        const unsigned char u = static_cast<unsigned char>(record.at(i));
        if (u >= 32 && u <= 126) {
            current.append(record.at(i));
        } else {
            const QString part = QString::fromLatin1(current).trimmed();
            if (!part.isEmpty())
                parts.append(part);
            current.clear();
        }
    }
    const QString last = QString::fromLatin1(current).trimmed();
    if (!last.isEmpty())
        parts.append(last);
    return parts.join(QLatin1Char(' '));
}

QString beChunkText(const IffForm &form, const QByteArray &id)
{
    for (const IffChunk &chunk : form.chunks) {
        if (chunk.id == id) {
            int nul = chunk.data.indexOf('\0');
            if (nul < 0)
                nul = chunk.data.size();
            return QString::fromLatin1(chunk.data.left(nul)).trimmed();
        }
    }
    return {};
}

} // namespace

bool SoundBankPlugin::isSatisfiedBy(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    if (looksLikeIff(data))
        return false; // a single IFF file is handled by IffPlugin

    int forms = 0;
    for (int i = 0; i + 12 <= data.size(); ++i) {
        if (data.mid(i, 4) != QByteArrayLiteral("FORM"))
            continue;
        const QByteArray type = data.mid(i + 8, 4);
        if (type == QByteArrayLiteral("8SVX") || type == QByteArrayLiteral("16SV")
            || type == QByteArrayLiteral("INST") || type == QByteArrayLiteral("ENVL")
            || type == QByteArrayLiteral("SMUS")) {
            ++forms;
            if (forms >= 2)
                return true;
        }
    }
    return false;
}

QWidget *SoundBankPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return textView(QStringLiteral("Unable to open the sound bank."));
    const QByteArray data = file.readAll();

    auto *tabs = new QTabWidget;
    int sampleCount = 0;
    int instCount = 0;
    int envlCount = 0;
    int otherCount = 0;
    QString definitions;
    QVector<QWidget *> audioTabs;

    for (int i = 0; i + 12 <= data.size(); ++i) {
        if (data.mid(i, 4) != QByteArrayLiteral("FORM"))
            continue;
        const quint32 size = (static_cast<quint8>(data.at(i + 4)) << 24)
                             | (static_cast<quint8>(data.at(i + 5)) << 16)
                             | (static_cast<quint8>(data.at(i + 6)) << 8)
                             | static_cast<quint8>(data.at(i + 7));
        if (i + 8 + static_cast<int>(size) > data.size())
            continue;

        const QByteArray formData = data.mid(i, static_cast<int>(size) + 8);
        IffForm form;
        if (!readIffForm(formData, form))
            continue;

        if (form.type == QByteArrayLiteral("8SVX") || form.type == QByteArrayLiteral("16SV")) {
            AudioSample sample;
            QString error;
            if (load8svx(formData, sample, &error)) {
                if (sample.name.isEmpty())
                    sample.name = QStringLiteral("Sample %1").arg(++sampleCount);
                audioTabs.append(new AudioView(sample));
            }
            continue;
        }

        if (form.type == QByteArrayLiteral("INST")) {
            ++instCount;
            definitions += QStringLiteral("INST  name=%1  PTCH=%2  VLUM=%3  SAMP=%4\n")
                               .arg(beChunkText(form, QByteArrayLiteral("NAME")),
                                    beChunkText(form, QByteArrayLiteral("PTCH")),
                                    beChunkText(form, QByteArrayLiteral("VLUM")),
                                    beChunkText(form, QByteArrayLiteral("SAMP")));
        } else if (form.type == QByteArrayLiteral("ENVL")) {
            ++envlCount;
            definitions += QStringLiteral("ENVL  %1 bytes\n").arg(formData.size());
        } else {
            ++otherCount;
            definitions += QStringLiteral("%1  %2 bytes\n")
                               .arg(QString::fromLatin1(form.type))
                               .arg(formData.size());
        }
    }

    QString summary;
    summary += QStringLiteral("Embedded IFF forms\n");
    summary += QStringLiteral("8SVX/16SV: %1\n").arg(audioTabs.size());
    summary += QStringLiteral("INST: %1\n").arg(instCount);
    summary += QStringLiteral("ENVL: %1\n").arg(envlCount);
    summary += QStringLiteral("Other: %1\n\n").arg(otherCount);
    summary += definitions;
    tabs->addTab(textView(summary), QStringLiteral("Summary"));

    int index = 1;
    for (QWidget *audio : audioTabs)
        tabs->addTab(audio, QStringLiteral("Sample %1").arg(index++));

    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

bool SaveFilePlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toUpper();
    const QString name = info.fileName().toUpper();
    return suffix == QStringLiteral("SAV")
           || suffix == QStringLiteral("SWG")
           || suffix == QStringLiteral("FX")
           || (name.startsWith(QStringLiteral("SAVGAM")) && suffix == QStringLiteral("DAT"));
}

QWidget *SaveFilePlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return textView(QStringLiteral("Unable to open the save file."));
    const QByteArray data = file.readAll();
    const QFileInfo info(parameters.filename);
    const QString suffix = info.suffix().toUpper();

    if (suffix == QStringLiteral("SAV")) {
        CharacterDocument document;
        QString error;
        if (loadCharacterDocument(parameters.filename, document, &error))
            return new CharacterView(document);
    }

    if (suffix == QStringLiteral("SWG")) {
        auto *table = new QTableWidget;
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({QStringLiteral("Slot"), QStringLiteral("Item"), QStringLiteral("Raw")});
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setFont(QFont(QStringLiteral("Courier New"), 9));
        const int recordSize = 72;
        const int count = data.size() / recordSize;
        table->setRowCount(count);
        for (int i = 0; i < count; ++i) {
            const QByteArray record = data.mid(i * recordSize, recordSize);
            table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            table->setItem(i, 1, new QTableWidgetItem(recordText(record, 40)));
            table->setItem(i, 2, new QTableWidgetItem(QString::fromLatin1(record.toHex(' '))));
        }
        table->resizeColumnsToContents();
        return table;
    }

    if (suffix == QStringLiteral("FX")) {
        auto *table = new QTableWidget;
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({QStringLiteral("Index"), QStringLiteral("Code"),
                                          QStringLiteral("Effect"), QStringLiteral("Fields"),
                                          QStringLiteral("Raw")});
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setFont(QFont(QStringLiteral("Courier New"), 9));
        const int recordSize = 10;
        const int count = data.size() / recordSize;
        table->setRowCount(count);
        for (int i = 0; i < count; ++i) {
            const QByteArray record = data.mid(i * recordSize, recordSize);
            const int code = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(record.constData()));
            table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            table->setItem(i, 1, new QTableWidgetItem(QString::number(code)));
            table->setItem(i, 2, new QTableWidgetItem(
                QString::fromLatin1(CharacterTables::specialAbility(code))));
            table->setItem(i, 3, new QTableWidgetItem(QString::fromLatin1(record.mid(2).toHex(' '))));
            table->setItem(i, 4, new QTableWidgetItem(QString::fromLatin1(record.toHex(' '))));
        }
        table->resizeColumnsToContents();
        return table;
    }

    auto *tabs = new QTabWidget;
    QString text;
    text += QStringLiteral("Save file: %1\n").arg(info.fileName());
    text += QStringLiteral("Size: %1 bytes\n\n").arg(data.size());
    text += QStringLiteral("Printable strings:\n");
    text += printableStrings(data);
    tabs->addTab(textView(text), QStringLiteral("Summary"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    return tabs;
}

bool PeriodTablePlugin::isSatisfiedBy(const QString &path) const
{
    return QFileInfo(path).fileName().toLower().contains(QStringLiteral("periodtable"));
}

QWidget *PeriodTablePlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return textView(QStringLiteral("Unable to open the period table."));
    const QByteArray data = file.readAll();

    auto *table = new QTableWidget;
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QStringLiteral("Index"), QStringLiteral("Period (BE)"),
                                      QStringLiteral("Period (LE)")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    const int count = data.size() / 2;
    table->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        const quint16 be = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar *>(data.constData() + i * 2));
        const quint16 le = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar *>(data.constData() + i * 2));
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        table->setItem(i, 1, new QTableWidgetItem(QString::number(be)));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(le)));
    }
    table->resizeColumnsToContents();
    return table;
}


bool AmigaItemsPlugin::isSatisfiedBy(const QString &path) const
{
    return QFileInfo(path).fileName().compare(QStringLiteral("items"), Qt::CaseInsensitive) == 0;
}

QWidget *AmigaItemsPlugin::createViewer(const PluginParameters &parameters)
{
    QFile file(parameters.filename);
    if (!file.open(QIODevice::ReadOnly))
        return textView(QStringLiteral("Unable to open the items table."));
    const QByteArray data = file.readAll();

    auto *tabs = new QTabWidget;
    auto *table = new QTableWidget;
    table->setColumnCount(10);
    table->setHorizontalHeaderLabels({
        QStringLiteral("Item"), QStringLiteral("0"), QStringLiteral("1"),
        QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
        QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"),
        QStringLiteral("Raw")
    });
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFont(QFont(QStringLiteral("Courier New"), 9));

    const int header = 2;
    const int recordSize = 8;
    const int count = qMax(0, (data.size() - header) / recordSize);
    table->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        const QByteArray record = data.mid(header + i * recordSize, recordSize);
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        for (int b = 0; b < recordSize; ++b) {
            table->setItem(i, b + 1, new QTableWidgetItem(
                QString::number(static_cast<quint8>(record.at(b)))));
        }
        table->setItem(i, 9, new QTableWidgetItem(QString::fromLatin1(record.toHex(' '))));
    }
    table->resizeColumnsToContents();
    tabs->addTab(table, QStringLiteral("Records"));

    QString summary;
    summary += QStringLiteral("Amiga items table\n");
    summary += QStringLiteral("Size: %1 bytes\n").arg(data.size());
    summary += QStringLiteral("Recovered %1 eight-byte templates after a 2-byte header.\n\n").arg(count);
    summary += QStringLiteral("The exact template field names are not documented in the\n");
    summary += QStringLiteral("original Gold Box Explorer; the byte columns preserve the\n");
    summary += QStringLiteral("values for comparison with ITEM*.DAX records.\n");
    tabs->addTab(textView(summary), QStringLiteral("Summary"));
    return tabs;
}
