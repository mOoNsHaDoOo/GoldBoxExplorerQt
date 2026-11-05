#include "plugins/plugins.h"

#include "parse/mxtx.h"
#include "widgets/audioview.h"
#include "widgets/hexview.h"

#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

QWidget *messageView(const QString &text)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(24);
    return label;
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

QString noteName(int note)
{
    static const char *names[] = {"C", "C#", "D", "D#", "E", "F",
                                  "F#", "G", "G#", "A", "A#", "B"};
    return QStringLiteral("%1%2")
        .arg(QString::fromLatin1(names[note % 12]))
        .arg(note / 12 - 1);
}

QTableWidget *makeSequenceView(const MxtxFile &file)
{
    auto *table = makeTable({QStringLiteral("Offset"), QStringLiteral("Time (ms)"),
                             QStringLiteral("Note"), QStringLiteral("Velocity"),
                             QStringLiteral("Duration")});
    const QVector<MxtxEvent> &events = file.events();
    table->setRowCount(events.size());
    int time = 0;
    for (int row = 0; row < events.size(); ++row) {
        const MxtxEvent &event = events.at(row);
        table->setItem(row, 0, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(event.offset, 4, 16, QLatin1Char('0'))));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(time)));
        table->setItem(row, 2, new QTableWidgetItem(noteName(event.note)));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(event.velocity)));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(event.duration)));
        time += event.delta;
    }
    table->resizeColumnsToContents();
    return table;
}

QImage renderPianoRoll(const MxtxFile &file)
{
    constexpr int minNote = 24;
    constexpr int maxNote = 96;
    constexpr int noteHeight = 4;
    const int width = 2400;
    const int height = (maxNote - minNote + 1) * noteHeight + 20;
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(28, 28, 32));

    QPainter painter(&image);
    for (int note = minNote; note <= maxNote; ++note) {
        if (note % 12 != 0)
            continue;
        const int y = 10 + (maxNote - note) * noteHeight;
        painter.setPen(QColor(55, 55, 62));
        painter.drawLine(0, y, width, y);
    }

    const int totalTicks = qMax(1, file.totalTicks());
    int time = 0;
    for (const MxtxEvent &event : file.events()) {
        const int x = 10 + (time * (width - 20)) / totalTicks;
        const int w = qMax(2, (qMax(1, event.duration) * (width - 20)) / totalTicks);
        const int note = qBound(minNote, event.note, maxNote);
        const int y = 10 + (maxNote - note) * noteHeight;
        painter.fillRect(x, y, w, noteHeight - 1, QColor(110, 220, 140));
        time += event.delta;
    }
    painter.end();
    return image;
}

QImage renderWaveforms(const MxtxFile &file)
{
    constexpr int width = 1200;
    constexpr int height = 300;
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(24, 24, 28));

    const QByteArray &data = file.data();
    const int start = qBound(0, file.eventEnd(), data.size());
    const int count = data.size() - start;
    if (count <= 0)
        return image;

    QPainter painter(&image);
    painter.setPen(QColor(110, 220, 140));
    const int columns = qMin(width, count);
    for (int x = 0; x < columns; ++x) {
        const int begin = start + (x * count) / columns;
        const int end = start + ((x + 1) * count) / columns;
        int low = 255;
        int high = 0;
        for (int i = begin; i < end && i < data.size(); ++i) {
            const int value = static_cast<quint8>(data.at(i));
            low = qMin(low, value);
            high = qMax(high, value);
        }
        if (low > high)
            continue;
        const int y1 = height - 10 - (high * (height - 20)) / 255;
        const int y2 = height - 10 - (low * (height - 20)) / 255;
        painter.drawLine(x, y1, x, qMax(y1 + 1, y2));
    }
    painter.end();
    return image;
}

} // namespace

bool MxtxPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    if (info.suffix().toUpper() != QStringLiteral("MX"))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return MxtxFile::looksLikeMxtx(file.read(16));
}

QWidget *MxtxPlugin::createViewer(const PluginParameters &parameters)
{
    MxtxFile file(parameters.filename);
    if (!file.isValid())
        return messageView(QStringLiteral("Unable to read this MXTX module:\n%1")
                               .arg(file.errorString()));

    auto *tabs = new QTabWidget;

    QString summary;
    summary += QStringLiteral("File: %1\n").arg(QFileInfo(parameters.filename).fileName());
    summary += QStringLiteral("Type: Music-X MXTX module\n");
    summary += QStringLiteral("Size: %1 bytes\n").arg(file.fileSize());
    summary += QStringLiteral("Header word: %1\n").arg(file.tempo());
    summary += QStringLiteral("Flags: %1\n").arg(file.flags());
    summary += QStringLiteral("Header count: %1\n").arg(file.headerCount());
    summary += QStringLiteral("Event stream: 0x%1..0x%2\n")
                   .arg(file.eventStart(), 4, 16, QLatin1Char('0'))
                   .arg(file.eventEnd(), 4, 16, QLatin1Char('0'));
    summary += QStringLiteral("Note events: %1\n").arg(file.events().size());
    summary += QStringLiteral("Total ticks: %1 (about %2 s)\n\n")
                   .arg(file.totalTicks())
                   .arg(file.totalTicks() / 1000.0, 0, 'f', 1);
    summary += QStringLiteral("The sequence uses six-byte events: delta, note, velocity, duration.\n");
    summary += QStringLiteral("The bytes after the sequence are the instrument waveform/envelope section.\n");
    auto *summaryView = new QPlainTextEdit(summary);
    summaryView->setReadOnly(true);
    summaryView->setFont(QFont(QStringLiteral("Courier New"), 10));
    tabs->addTab(summaryView, QStringLiteral("Summary"));

    tabs->addTab(makeSequenceView(file), QStringLiteral("Sequence"));

    auto *rollScroll = new QScrollArea;
    auto *rollLabel = new QLabel;
    rollLabel->setPixmap(QPixmap::fromImage(renderPianoRoll(file)));
    rollLabel->resize(rollLabel->pixmap().size());
    rollScroll->setWidget(rollLabel);
    tabs->addTab(rollScroll, QStringLiteral("Piano Roll"));

    const AudioSample sample = MxtxFile::synthesize(file);
    if (sample.valid)
        tabs->addTab(new AudioView(sample), QStringLiteral("Music"));

    auto *waveScroll = new QScrollArea;
    auto *waveLabel = new QLabel;
    waveLabel->setPixmap(QPixmap::fromImage(renderWaveforms(file)));
    waveLabel->resize(waveLabel->pixmap().size());
    waveScroll->setWidget(waveLabel);
    tabs->addTab(waveScroll, QStringLiteral("Waveforms"));

    QFile raw(parameters.filename);
    if (raw.open(QIODevice::ReadOnly))
        tabs->addTab(new HexView(raw.readAll()), QStringLiteral("Hex"));

    tabs->setCurrentIndex(1);
    return tabs;
}
