#include "plugins/plugins.h"

#include "parse/slb.h"
#include "widgets/audioview.h"
#include "widgets/hexview.h"

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

#include <algorithm>

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

QString eventText(const SlbEvent &e)
{
    const int length = SlbFile::lengthOf(e.data);
    if (e.isEnd())
        return QStringLiteral("end of track");
    if (e.isNote())
        return QStringLiteral("note %1 (%2), length %3%4")
            .arg(noteName(e.code)).arg(e.code).arg(length).arg((e.data & 0x40) ? QStringLiteral(", tied") : QString());
    if (e.isRest())
        return QStringLiteral("rest, length %1").arg(length);
    if (e.code == 0x81)
        return QStringLiteral("voice off, instrument %1").arg(e.data);
    if (e.code == 0x84)
        return QStringLiteral("volume %1").arg(e.data);
    if (e.code <= 0x80)
        return QStringLiteral("%1 with bit 7 of the length set (skipped)").arg(e.code == 0x80 ? QStringLiteral("rest") : noteName(e.code));
    return QStringLiteral("command $%1 %2 (ignored)").arg(e.code, 2, 16, QLatin1Char('0')).arg(e.data);
}

QWidget *makeSongsView(const QVector<SlbSong> &songs)
{
    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Offset"), QStringLiteral("Tempo"),
                             QStringLiteral("Volume"), QStringLiteral("Tracks"), QStringLiteral("Notes")});
    table->setRowCount(songs.size());
    for (int row = 0; row < songs.size(); ++row) {
        const SlbSong &song = songs.at(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(song.index + 1)));
        table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0x%1").arg(song.offset, 4, 16, QLatin1Char('0'))));
        table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("$%1 (factor %2)")
                                                        .arg(song.tempo, 4, 16, QLatin1Char('0')).arg(song.tempoFactor())));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(song.volume)));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(song.tracks.size())));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(song.noteCount())));
    }
    table->resizeColumnsToContents();
    return table;
}

QWidget *makeEventsView(const SlbSong &song)
{
    auto *tabs = new QTabWidget;
    for (int v = 0; v < song.tracks.size(); ++v) {
        const SlbTrack &track = song.tracks.at(v);
        auto *table = makeTable({QStringLiteral("Offset"), QStringLiteral("Bytes"), QStringLiteral("Event")});
        table->setRowCount(track.events.size());
        for (int row = 0; row < track.events.size(); ++row) {
            const SlbEvent &e = track.events.at(row);
            table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("0x%1").arg(e.offset, 4, 16, QLatin1Char('0'))));
            table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1 %2")
                                                            .arg(e.code, 2, 16, QLatin1Char('0'))
                                                            .arg(e.data, 2, 16, QLatin1Char('0'))));
            table->setItem(row, 2, new QTableWidgetItem(eventText(e)));
        }
        table->resizeColumnsToContents();
        tabs->addTab(table, v < song.firstTrack ? QStringLiteral("Track %1 (not played)").arg(v + 1)
                                                : QStringLiteral("Track %1").arg(v + 1));
    }
    return tabs;
}

QImage renderPianoRoll(const SlbSong &song)
{
    constexpr double pxPerUnit = 0.5;   // 5 ms units
    constexpr int noteHeight = 4;
    int minNote = 127, maxNote = 0;
    qint64 total = 0;
    for (const SlbTrack &track : song.tracks) {
        qint64 t = 0;
        for (const SlbEvent &e : track.events) {
            if (e.isNote()) {
                minNote = std::min(minNote, static_cast<int>(e.code));
                maxNote = std::max(maxNote, static_cast<int>(e.code));
            }
            if (e.isNote() || e.isRest())
                t += SlbFile::lengthOf(e.data) / std::max(1, song.tempoFactor());
        }
        total = std::max(total, t);
    }
    if (minNote > maxNote) {
        minNote = 48;
        maxNote = 72;
    }
    minNote -= 2;
    maxNote += 2;
    const int width = std::max(320, static_cast<int>(total * pxPerUnit) + 20);
    const int height = (maxNote - minNote + 1) * noteHeight + 20;
    QImage image(std::min(width, 30000), height, QImage::Format_ARGB32);
    image.fill(QColor(28, 28, 32));
    QPainter painter(&image);
    for (int note = minNote; note <= maxNote; ++note) {
        if (note % 12 != 0)
            continue;
        painter.setPen(QColor(55, 55, 62));
        const int y = 10 + (maxNote - note) * noteHeight;
        painter.drawLine(0, y, width, y);
    }
    static const QColor colors[5] = {QColor(110, 220, 140), QColor(110, 170, 255), QColor(255, 196, 64),
                                     QColor(240, 120, 160), QColor(200, 200, 200)};
    for (int v = song.firstTrack; v < song.tracks.size(); ++v) {
        qint64 t = 0;
        for (const SlbEvent &e : song.tracks.at(v).events) {
            if (!e.isNote() && !e.isRest())
                continue;
            const qint64 units = SlbFile::lengthOf(e.data) / std::max(1, song.tempoFactor());
            if (e.isNote()) {
                const int x = 10 + static_cast<int>(t * pxPerUnit);
                const int w = std::max(2, static_cast<int>(units * pxPerUnit) - 1);
                const int y = 10 + (maxNote - e.code) * noteHeight;
                painter.fillRect(x, y, w, noteHeight - 1, colors[v % 5]);
            }
            t += units;
        }
    }
    painter.end();
    return image;
}

// A song tab: the rendered music on top, its tracks and piano roll below.
QWidget *makeSongTab(const SlbSong &song)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    const AudioSample sample = SlbFile::render(song);
    if (sample.valid)
        layout->addWidget(new AudioView(sample));
    else
        layout->addWidget(messageView(QStringLiteral("The song does not play: %1").arg(sample.error)));
    auto *inner = new QTabWidget;
    inner->addTab(makeEventsView(song), QStringLiteral("Tracks"));
    auto *scroll = new QScrollArea;
    auto *label = new QLabel;
    label->setPixmap(QPixmap::fromImage(renderPianoRoll(song)));
    label->resize(label->pixmap().size());
    scroll->setWidget(label);
    inner->addTab(scroll, QStringLiteral("Piano roll"));
    layout->addWidget(inner, 1);
    return page;
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
        return messageView(QStringLiteral("Unable to read this SLB song bank:\n%1").arg(file.errorString()));

    const QVector<SlbSong> songs = file.songs();
    auto *tabs = new QTabWidget;

    QString summary;
    summary += QStringLiteral("File: %1 (%2 bytes)\n").arg(QFileInfo(parameters.filename).fileName()).arg(file.fileSize());
    summary += QStringLiteral("Songs: %1\n\n").arg(songs.size());
    summary += QStringLiteral(
        "SLB song bank of the Amiga Gold Box games. Curse of the Azure Bonds plays its\n"
        "music from CURSE.SLB; Dark Queen of Krynn loads MUSIC.SLB when the sampled\n"
        ".MX music is not loaded (Secret of the Silver Blades links the same code).\n\n"
        "Each song has up to five tracks of two-byte events (note or rest with a\n"
        "length, volume and voice-off commands, $FF at the end); the song starts again\n"
        "when every track has ended. The game's sequencer runs every 10 ms and plays\n"
        "the notes on Paula with a sawtooth wave, the rules reconstructed from the\n"
        "Curse executable (the four games link the same code). The music tabs play\n"
        "one pass and the start of the next, fading out.\n");
    auto *summaryView = new QPlainTextEdit(summary);
    summaryView->setReadOnly(true);
    summaryView->setFont(QFont(QStringLiteral("Courier New"), 10));
    tabs->addTab(summaryView, QStringLiteral("Summary"));
    tabs->addTab(makeSongsView(songs), QStringLiteral("Songs"));

    // build a song tab only when it is opened (rendering takes a moment)
    QVector<QWidget *> pages;
    for (const SlbSong &song : songs) {
        auto *page = new QWidget;
        page->setLayout(new QVBoxLayout);
        page->layout()->setContentsMargins(0, 0, 0, 0);
        pages.append(page);
        tabs->addTab(page, QStringLiteral("Song %1").arg(song.index + 1));
    }
    QObject::connect(tabs, &QTabWidget::currentChanged, tabs, [tabs, pages, songs](int index) {
        const int song = pages.indexOf(tabs->widget(index));
        if (song < 0 || pages.at(song)->property("loaded").toBool())
            return;
        pages.at(song)->layout()->addWidget(makeSongTab(songs.at(song)));
        pages.at(song)->setProperty("loaded", true);
    });

    QFile raw(parameters.filename);
    if (raw.open(QIODevice::ReadOnly))
        tabs->addTab(new HexView(raw.readAll()), QStringLiteral("Hex"));

    tabs->setCurrentIndex(songs.isEmpty() ? 0 : 2);
    return tabs;
}
