#include "plugins/plugins.h"

#include "parse/slb.h"
#include "widgets/audioview.h"
#include "widgets/hexview.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
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

QString eventType(const SlbEvent &event)
{
    if (event.end)
        return QStringLiteral("End");
    if (event.note)
        return QStringLiteral("Note");
    if (event.rest)
        return QStringLiteral("Rest");
    switch (event.command) {
    case 0x81: return QStringLiteral("Instrument");
    case 0x82: return QStringLiteral("Volume");
    case 0x83: return QStringLiteral("Transpose");
    case 0x84: return QStringLiteral("Envelope");
    case 0x87: return QStringLiteral("Pattern");
    case 0x88: return QStringLiteral("Tempo");
    default: return QStringLiteral("Command");
    }
}

QWidget *makeSongsView(const QVector<SlbSong> &songs)
{
    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Start"),
                             QStringLiteral("End"), QStringLiteral("Loop"),
                             QStringLiteral("Events"), QStringLiteral("Notes"),
                             QStringLiteral("Units")});
    table->setRowCount(songs.size());
    for (int row = 0; row < songs.size(); ++row) {
        const SlbSong &song = songs.at(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(song.index + 1)));
        table->setItem(row, 1, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(song.start, 4, 16, QLatin1Char('0'))));
        table->setItem(row, 2, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(song.end, 4, 16, QLatin1Char('0'))));
        table->setItem(row, 3, new QTableWidgetItem(
            song.loopPoint > 0
                ? QStringLiteral("0x%1").arg(song.loopPoint, 4, 16, QLatin1Char('0'))
                : QStringLiteral("-")));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(song.events.size())));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(song.noteCount)));
        table->setItem(row, 6, new QTableWidgetItem(QString::number(song.totalUnits)));
    }
    table->resizeColumnsToContents();
    return table;
}

QTableWidget *makeEventsTable(const SlbSong &song)
{
    auto *table = makeTable({QStringLiteral("Offset"), QStringLiteral("Type"),
                             QStringLiteral("Note"), QStringLiteral("Duration"),
                             QStringLiteral("Raw")});
    table->setRowCount(song.events.size());
    for (int row = 0; row < song.events.size(); ++row) {
        const SlbEvent &event = song.events.at(row);
        table->setItem(row, 0, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(event.offset, 4, 16, QLatin1Char('0'))));
        table->setItem(row, 1, new QTableWidgetItem(eventType(event)));
        table->setItem(row, 2, new QTableWidgetItem(
            event.note ? noteName(event.command) : QString()));
        table->setItem(row, 3, new QTableWidgetItem(
            (event.note || event.rest || event.end)
                ? QString::number(event.duration)
                : QString()));
        table->setItem(row, 4, new QTableWidgetItem(
            QStringLiteral("%1 %2")
                .arg(event.command, 2, 16, QLatin1Char('0'))
                .arg(event.data, 2, 16, QLatin1Char('0'))));
    }
    table->resizeColumnsToContents();
    return table;
}

QImage renderPianoRoll(const SlbSong &song)
{
    constexpr int pxPerUnit = 3;
    constexpr int noteHeight = 4;
    constexpr int minNote = 24;
    constexpr int maxNote = 96;
    const int width = qMax(320, song.totalUnits * pxPerUnit + 20);
    const int height = (maxNote - minNote + 1) * noteHeight + 20;

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(28, 28, 32));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Octave lines and a beat grid.
    for (int note = minNote; note <= maxNote; ++note) {
        if (note % 12 != 0)
            continue;
        const int y = 10 + (maxNote - note) * noteHeight;
        painter.setPen(QColor(55, 55, 62));
        painter.drawLine(0, y, width, y);
    }
    for (int unit = 0; unit < song.totalUnits; unit += 16) {
        const int x = 10 + unit * pxPerUnit;
        painter.setPen(QColor(45, 45, 52));
        painter.drawLine(x, 0, x, height);
    }

    int time = 0;
    for (const SlbEvent &event : song.events) {
        const int units = qMax(1, event.duration);
        if (event.note) {
            const int note = qBound(minNote, static_cast<int>(event.command), maxNote);
            const int x = 10 + time * pxPerUnit;
            const int w = qMax(2, units * pxPerUnit - 1);
            const int y = 10 + (maxNote - note) * noteHeight;
            const bool tied = (event.data & 0x40) != 0;
            painter.fillRect(x, y, w, noteHeight - 1,
                             tied ? QColor(255, 196, 64) : QColor(110, 220, 140));
            time += units;
        } else if (event.rest) {
            time += units;
        }
    }
    painter.end();
    return image;
}

QWidget *makePianoRollTab(const QVector<SlbSong> &songs)
{
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *combo = new QComboBox;
    for (const SlbSong &song : songs)
        combo->addItem(QStringLiteral("Song %1").arg(song.index + 1));
    layout->addWidget(combo);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(false);
    auto *label = new QLabel;
    scroll->setWidget(label);
    layout->addWidget(scroll, 1);

    auto update = [combo, label, songs]() {
        const int index = combo->currentIndex();
        if (index < 0 || index >= songs.size())
            return;
        label->setPixmap(QPixmap::fromImage(renderPianoRoll(songs.at(index))));
        label->resize(label->pixmap().size());
    };
    QObject::connect(combo, &QComboBox::currentIndexChanged, label, update);
    if (!songs.isEmpty())
        update();
    return widget;
}

QWidget *makeMusicTab(const QVector<SlbSong> &songs)
{
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *combo = new QComboBox;
    for (const SlbSong &song : songs)
        combo->addItem(QStringLiteral("Song %1").arg(song.index + 1));
    layout->addWidget(combo);

    auto *host = new QWidget;
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(host, 1);

    auto update = [combo, hostLayout, songs]() {
        while (QLayoutItem *item = hostLayout->takeAt(0)) {
            if (QWidget *old = item->widget())
                old->deleteLater();
            delete item;
        }
        const int index = combo->currentIndex();
        if (index < 0 || index >= songs.size())
            return;
        const AudioSample sample = SlbFile::synthesize(songs.at(index));
        if (sample.valid)
            hostLayout->addWidget(new AudioView(sample));
    };
    QObject::connect(combo, &QComboBox::currentIndexChanged, host, update);
    if (!songs.isEmpty())
        update();
    return widget;
}

} // namespace

bool SlbPlugin::isSatisfiedBy(const QString &path) const
{
    const QFileInfo info(path);
    if (info.suffix().toUpper() != QStringLiteral("SLB"))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return SlbFile::looksLikeSlb(file.readAll());
}

QWidget *SlbPlugin::createViewer(const PluginParameters &parameters)
{
    SlbFile file(parameters.filename);
    if (!file.isValid())
        return messageView(QStringLiteral("Unable to read this SLB song bank:\n%1")
                               .arg(file.errorString()));

    const QVector<SlbSong> songs = file.songs();
    auto *tabs = new QTabWidget;

    QString summary;
    summary += QStringLiteral("File: %1\n").arg(QFileInfo(parameters.filename).fileName());
    summary += QStringLiteral("Type: SLBR Amiga song bank\n");
    summary += QStringLiteral("Size: %1 bytes\n").arg(file.fileSize());
    summary += QStringLiteral("Songs: %1\n\n").arg(songs.size());
    summary += QStringLiteral("The song streams use two-byte events: note/command + data.\n");
    summary += QStringLiteral("Note durations are stored in the low six bits; 0x40 marks a tied note.\n");
    summary += QStringLiteral("0xff 0xff terminates each song.\n");
    auto *summaryView = new QPlainTextEdit(summary);
    summaryView->setReadOnly(true);
    summaryView->setFont(QFont(QStringLiteral("Courier New"), 10));
    tabs->addTab(summaryView, QStringLiteral("Summary"));

    tabs->addTab(makeSongsView(songs), QStringLiteral("Songs"));

    auto *eventsWidget = new QWidget;
    auto *eventsLayout = new QVBoxLayout(eventsWidget);
    eventsLayout->setContentsMargins(0, 0, 0, 0);
    auto *eventsCombo = new QComboBox;
    for (const SlbSong &song : songs)
        eventsCombo->addItem(QStringLiteral("Song %1").arg(song.index + 1));
    eventsLayout->addWidget(eventsCombo);
    auto *eventsHost = new QWidget;
    auto *eventsHostLayout = new QVBoxLayout(eventsHost);
    eventsHostLayout->setContentsMargins(0, 0, 0, 0);
    eventsLayout->addWidget(eventsHost, 1);
    auto updateEvents = [eventsCombo, eventsHostLayout, songs]() {
        while (QLayoutItem *item = eventsHostLayout->takeAt(0)) {
            if (QWidget *old = item->widget())
                old->deleteLater();
            delete item;
        }
        const int index = eventsCombo->currentIndex();
        if (index >= 0 && index < songs.size())
            eventsHostLayout->addWidget(makeEventsTable(songs.at(index)));
    };
    QObject::connect(eventsCombo, &QComboBox::currentIndexChanged, eventsHost, updateEvents);
    if (!songs.isEmpty())
        updateEvents();
    tabs->addTab(eventsWidget, QStringLiteral("Events"));

    tabs->addTab(makePianoRollTab(songs), QStringLiteral("Piano Roll"));
    tabs->addTab(makeMusicTab(songs), QStringLiteral("Music"));

    QFile raw(parameters.filename);
    if (raw.open(QIODevice::ReadOnly))
        tabs->addTab(new HexView(raw.readAll()), QStringLiteral("Hex"));

    tabs->setCurrentIndex(1); // Songs is the most useful starting point.
    return tabs;
}
