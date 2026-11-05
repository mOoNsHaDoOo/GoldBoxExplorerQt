#include "plugins/plugins.h"

#include "parse/ecl.h"
#include "parse/glb.h"
#include "widgets/audioview.h"
#include "widgets/hexview.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtEndian>

namespace {

QWidget *messageView(const QString &text)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(24);
    return label;
}

QString hexDump(const QByteArray &data, int maxBytes = 0)
{
    const int count = maxBytes > 0 ? qMin(maxBytes, data.size()) : data.size();
    QString out;
    for (int i = 0; i < count; i += 16) {
        out += QStringLiteral("%1  ").arg(i, 6, 16, QLatin1Char('0'));
        QString ascii;
        for (int j = 0; j < 16 && i + j < count; ++j) {
            const quint8 value = static_cast<quint8>(data.at(i + j));
            out += QStringLiteral("%1 ").arg(value, 2, 16, QLatin1Char('0'));
            ascii += (value >= 32 && value < 127) ? QChar(value) : QLatin1Char('.');
        }
        out += QLatin1Char(' ') + ascii + QLatin1Char('\n');
    }
    if (maxBytes > 0 && data.size() > maxBytes)
        out += QStringLiteral("... %1 more bytes\n").arg(data.size() - maxBytes);
    return out;
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget;
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    table->setFont(QFont(QStringLiteral("Courier New"), 9));
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

AudioSample sampleFromBlock(const QByteArray &data, int index)
{
    AudioSample sample;
    sample.name = QStringLiteral("Sample %1").arg(index + 1);
    sample.sampleRate = 11025;
    sample.channels = 1;
    sample.bits = 16;

    int start = 0;
    int length = data.size();
    // DIG8 blocks carry a small header: 1c fa 00 00 <u16 length>.
    if (data.size() >= 6
        && static_cast<quint8>(data.at(0)) == 0x1C
        && static_cast<quint8>(data.at(1)) == 0xFA) {
        const int declared = (static_cast<quint8>(data.at(4)) << 8)
                             | static_cast<quint8>(data.at(5));
        start = 6;
        length = qMin(declared, data.size() - start);
    }
    if (length <= 0)
        return sample;

    sample.frameCount = length;
    sample.pcm.resize(length * 2);
    for (int i = 0; i < length; ++i) {
        const qint8 value = static_cast<qint8>(data.at(start + i));
        qToLittleEndian<qint16>(static_cast<qint16>(value) * 256,
                                sample.pcm.data() + i * 2);
    }
    sample.valid = true;
    return sample;
}

QWidget *makeBlockBrowser(const QVector<GlbBlock> &blocks)
{
    auto *splitter = new QSplitter(Qt::Vertical);

    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Offset"),
                             QStringLiteral("Size"), QStringLiteral("Preview")});
    table->setRowCount(blocks.size());
    for (int row = 0; row < blocks.size(); ++row) {
        const GlbBlock &block = blocks.at(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(block.index)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(block.offset)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(block.data.size())));
        table->setItem(row, 3, new QTableWidgetItem(
            QString::fromLatin1(block.data.left(48).toHex(' '))));
    }
    table->resizeColumnsToContents();

    auto *hex = new QPlainTextEdit;
    hex->setReadOnly(true);
    hex->setLineWrapMode(QPlainTextEdit::NoWrap);
    hex->setFont(QFont(QStringLiteral("Courier New"), 9));

    auto *container = new QWidget;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *exportButton = new QPushButton(QStringLiteral("Export blocks..."));
    layout->addWidget(exportButton);
    layout->addWidget(splitter, 1);
    splitter->addWidget(table);
    splitter->addWidget(hex);

    QObject::connect(table, &QTableWidget::currentCellChanged, hex,
                     [hex, blocks](int row, int, int, int) {
                         if (row >= 0 && row < blocks.size())
                             hex->setPlainText(hexDump(blocks.at(row).data));
                     });
    QObject::connect(exportButton, &QPushButton::clicked, exportButton,
                     [exportButton, blocks]() {
                         const QString dir = QFileDialog::getExistingDirectory(
                             exportButton, QStringLiteral("Export GLB blocks"));
                         if (dir.isEmpty())
                             return;
                         const QString base = QStringLiteral("block");
                         for (const GlbBlock &block : blocks) {
                             QFile out(QDir(dir).filePath(
                                 QStringLiteral("%1_%2.bin")
                                     .arg(base)
                                     .arg(block.index, 3, 10, QLatin1Char('0'))));
                             if (out.open(QIODevice::WriteOnly))
                                 out.write(block.data);
                         }
                     });
    if (!blocks.isEmpty())
        table->selectRow(0);
    return container;
}

QWidget *makeStringsView(const QVector<GlbBlock> &blocks)
{
    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("String")});
    table->setRowCount(blocks.size());
    for (int row = 0; row < blocks.size(); ++row) {
        QByteArray text = blocks.at(row).data;
        while (text.endsWith('\0'))
            text.chop(1);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(row)));
        table->setItem(row, 1, new QTableWidgetItem(QString::fromLatin1(text)));
    }
    table->resizeColumnsToContents();
    return table;
}

QWidget *makeAudioView(const QVector<GlbBlock> &blocks)
{
    auto *tabs = new QTabWidget;
    for (int i = 0; i < blocks.size(); ++i) {
        const AudioSample sample = sampleFromBlock(blocks.at(i).data, i);
        if (!sample.valid)
            continue;
        tabs->addTab(new AudioView(sample),
                     QStringLiteral("Sample %1").arg(i + 1));
    }
    if (tabs->count() == 0)
        return messageView(QStringLiteral("No DIG8 samples could be decoded."));
    return tabs;
}

QWidget *makeItemView(const QVector<GlbBlock> &blocks)
{
    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Name"),
                             QStringLiteral("Bytes"), QStringLiteral("Data")});
    table->setRowCount(blocks.size());
    for (int row = 0; row < blocks.size(); ++row) {
        const QByteArray &data = blocks.at(row).data;
        const int nameLength = data.isEmpty()
                                   ? 0
                                   : static_cast<quint8>(data.at(0));
        const QString name = (nameLength > 0 && nameLength + 1 <= data.size())
                                 ? QString::fromLatin1(data.mid(1, nameLength))
                                 : QString();
        table->setItem(row, 0, new QTableWidgetItem(QString::number(row)));
        table->setItem(row, 1, new QTableWidgetItem(name));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(data.size())));
        table->setItem(row, 3, new QTableWidgetItem(
            QString::fromLatin1(data.mid(nameLength + 1).left(72).toHex(' '))));
    }
    table->resizeColumnsToContents();
    return table;
}

QWidget *makeMonsterView(const QVector<GlbBlock> &blocks)
{
    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Name"),
                             QStringLiteral("Bytes"), QStringLiteral("Data")});
    table->setRowCount(blocks.size());
    for (int row = 0; row < blocks.size(); ++row) {
        const QByteArray &data = blocks.at(row).data;
        const int nameLength = data.size() > 1
                                   ? static_cast<quint8>(data.at(1))
                                   : 0;
        const QString name = (nameLength > 0 && nameLength + 2 <= data.size())
                                 ? QString::fromLatin1(data.mid(2, nameLength))
                                 : QString();
        table->setItem(row, 0, new QTableWidgetItem(QString::number(row)));
        table->setItem(row, 1, new QTableWidgetItem(name));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(data.size())));
        table->setItem(row, 3, new QTableWidgetItem(
            QString::fromLatin1(data.left(96).toHex(' '))));
    }
    table->resizeColumnsToContents();
    return table;
}

QWidget *makeRecordView(const QVector<GlbBlock> &blocks, int recordSize)
{
    auto *table = makeTable({QStringLiteral("Block"), QStringLiteral("Record"),
                             QStringLiteral("Bytes"), QStringLiteral("Data")});
    int rows = 0;
    for (const GlbBlock &block : blocks)
        rows += recordSize > 0 ? (block.data.size() + recordSize - 1) / recordSize : 1;
    table->setRowCount(rows);

    int row = 0;
    for (const GlbBlock &block : blocks) {
        const int records = recordSize > 0
                                ? qMax(1, (block.data.size() + recordSize - 1) / recordSize)
                                : 1;
        for (int r = 0; r < records; ++r) {
            const int begin = r * recordSize;
            const QByteArray part = recordSize > 0
                                        ? block.data.mid(begin, recordSize)
                                        : block.data;
            table->setItem(row, 0, new QTableWidgetItem(QString::number(block.index)));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(r)));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(part.size())));
            table->setItem(row, 3, new QTableWidgetItem(
                QString::fromLatin1(part.toHex(' '))));
            ++row;
        }
    }
    table->resizeColumnsToContents();
    return table;
}

QWidget *makeEclView(const QVector<GlbBlock> &blocks)
{
    QString text;
    for (const GlbBlock &block : blocks) {
        const QStringList strings = EclFile::extractStrings(block.data);
        if (strings.isEmpty())
            continue;
        text += QStringLiteral("=== Block %1 ===\n").arg(block.index);
        text += strings.join(QLatin1Char('\n'));
        text += QLatin1String("\n\n");
    }
    if (text.isEmpty())
        text = QStringLiteral("No printable ECL strings were found.");
    auto *view = new QPlainTextEdit(text);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFont(QStringLiteral("Courier New"), 10));
    return view;
}

QWidget *makeSimpleBlockTable(const QVector<GlbBlock> &blocks)
{
    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Offset"),
                             QStringLiteral("Size"), QStringLiteral("Preview")});
    table->setRowCount(blocks.size());
    for (int row = 0; row < blocks.size(); ++row) {
        const GlbBlock &block = blocks.at(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(block.index)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(block.offset)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(block.data.size())));
        table->setItem(row, 3, new QTableWidgetItem(
            QString::fromLatin1(block.data.left(64).toHex(' '))));
    }
    table->resizeColumnsToContents();
    return table;
}

} // namespace

bool GlbPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    if (info.suffix().toUpper() != QStringLiteral("GLB"))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return GlbFile::looksLikeGlb(file.readAll());
}

QWidget *GlbPlugin::createViewer(const PluginParameters &parameters)
{
    GlbFile file(parameters.filename);
    if (!file.isValid())
        return messageView(QStringLiteral("Unable to read this GLB container:\n%1")
                               .arg(file.errorString()));

    const QString fileName = QFileInfo(parameters.filename).fileName().toUpper();
    const QVector<GlbBlock> blocks = file.blocks();

    auto *tabs = new QTabWidget;

    QString summary;
    summary += QStringLiteral("File: %1\n").arg(QFileInfo(parameters.filename).fileName());
    summary += QStringLiteral("Container: %1\n")
                   .arg(file.isBigEndian() ? QStringLiteral("GLIB (big-endian)")
                                           : QStringLiteral("GLIB (little-endian)"));
    summary += QStringLiteral("Content type: %1\n").arg(file.contentType());
    summary += QStringLiteral("Entries: %1\n").arg(file.entryCount());
    summary += QStringLiteral("Decoded blocks: %1\n").arg(blocks.size());
    if (!file.idTable().isEmpty()) {
        summary += QStringLiteral("ID table: ");
        const int pairs = file.idTable().size() / 2;
        for (int i = 0; i < qMin(pairs, 16); ++i)
            summary += QStringLiteral("%1->%2 ")
                           .arg(file.idTable().at(i * 2))
                           .arg(file.idTable().at(i * 2 + 1));
        if (pairs > 16)
            summary += QStringLiteral("...");
        summary += QLatin1Char('\n');
    }
    auto *summaryView = new QPlainTextEdit(summary);
    summaryView->setReadOnly(true);
    summaryView->setFont(QFont(QStringLiteral("Courier New"), 10));
    tabs->addTab(summaryView, QStringLiteral("Container"));

    tabs->addTab(makeBlockBrowser(blocks), QStringLiteral("Blocks"));

    const QString contentType = file.contentType().toUpper();
    if (contentType == QStringLiteral("TEXT")) {
        tabs->addTab(makeStringsView(blocks), QStringLiteral("Strings"));
    } else if (contentType == QStringLiteral("DIG8")) {
        tabs->addTab(makeAudioView(blocks), QStringLiteral("Audio"));
    } else if (contentType == QStringLiteral("DATA")) {
        if (fileName.contains(QStringLiteral("ECL")))
            tabs->addTab(makeEclView(blocks), QStringLiteral("ECL"));
        else if (fileName.contains(QStringLiteral("ITEM")))
            tabs->addTab(makeItemView(blocks), QStringLiteral("Items"));
        else if (fileName.contains(QStringLiteral("MONCHA")))
            tabs->addTab(makeMonsterView(blocks), QStringLiteral("Monsters"));
        else if (fileName.contains(QStringLiteral("MONSPC")))
            tabs->addTab(makeRecordView(blocks, 9), QStringLiteral("Effects"));
        else if (fileName.contains(QStringLiteral("MONITM")))
            tabs->addTab(makeRecordView(blocks, 0), QStringLiteral("Records"));
        else
            tabs->addTab(makeSimpleBlockTable(blocks), QStringLiteral("Records"));
    }

    QFile raw(parameters.filename);
    if (raw.open(QIODevice::ReadOnly))
        tabs->addTab(new HexView(raw.readAll()), QStringLiteral("Hex"));

    return tabs;
}
