#include "plugins/plugins.h"

#include "parse/mxmusic.h"
#include "widgets/audioview.h"
#include "widgets/hexview.h"

#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
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

const char *const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

QString noteName(int note)
{
    return QStringLiteral("%1%2").arg(QLatin1String(kNoteNames[note % 12])).arg(note / 12 - 1);
}

QString eventText(const MxEvent &e)
{
    if (e.status < 0x80)
        return QStringLiteral("note %1 (%2) velocity %3 channel %4 length %5")
            .arg(noteName(e.status)).arg(e.status).arg((e.data >> 1) & 0x78).arg(e.data & 0x0F).arg(e.word);
    switch (e.status) {
    case 0x80:
        return e.word ? QStringLiteral("tempo %1 BPM, ramp over %2 ticks").arg(e.data).arg(e.word)
                      : QStringLiteral("tempo %1 BPM").arg(e.data);
    case 0xB0:
        return QStringLiteral("controller %1 = %2, channel %3%4")
            .arg(e.b4).arg(e.b5).arg(e.data & 0x0F)
            .arg(e.b4 == 7 ? QStringLiteral(" (volume)") : e.b4 == 10 ? QStringLiteral(" (pan)") : QString());
    case 0xC0:
        return QStringLiteral("program %1, channel %2").arg(e.b5).arg(e.data & 0x0F);
    case 0xE0:
        return QStringLiteral("pitch bend %1, channel %2").arg(((e.b4 & 0x7F) << 7 | (e.b5 & 0x7F)) - 0x2000).arg(e.data & 0x0F);
    case 0xA0:
        return e.b4 == 1 ? QStringLiteral("signal %1 to the game").arg(e.b5)
               : e.b4 == 2 ? QStringLiteral("repeat start, %1 times").arg(e.b5)
               : e.b4 == 3 ? QStringLiteral("repeat end") : QStringLiteral("command %1 %2").arg(e.b4).arg(e.b5);
    case 0xFF:
        return QStringLiteral("end of song");
    default:
        return QStringLiteral("status $%1").arg(e.status, 2, 16, QLatin1Char('0'));
    }
}

QWidget *songView(const MxMusicFile &file, const MxSong &song)
{
    auto *splitter = new QSplitter(Qt::Vertical);
    const AudioSample audio = file.render(song.index);
    if (audio.valid)
        splitter->addWidget(new AudioView(audio));
    else
        splitter->addWidget(messageView(QStringLiteral("The song does not play: %1").arg(audio.error)));

    auto *table = makeTable({QStringLiteral("#"), QStringLiteral("Tick"), QStringLiteral("Delta"),
                             QStringLiteral("Bytes"), QStringLiteral("Event")});
    table->setRowCount(song.events.size());
    qint64 tick = 0;
    for (int i = 0; i < song.events.size(); ++i) {
        const MxEvent &e = song.events.at(i);
        tick += e.delta;
        const QByteArray raw = QByteArray(1, static_cast<char>(e.status)) + QByteArray(1, static_cast<char>(e.data))
                               + QByteArray(1, static_cast<char>(e.delta >> 8)) + QByteArray(1, static_cast<char>(e.delta))
                               + QByteArray(1, static_cast<char>(e.b4)) + QByteArray(1, static_cast<char>(e.b5));
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        table->setItem(i, 1, new QTableWidgetItem(QString::number(tick)));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(e.delta)));
        table->setItem(i, 3, new QTableWidgetItem(QString::fromLatin1(raw.toHex(' '))));
        table->setItem(i, 4, new QTableWidgetItem(eventText(e)));
    }
    table->resizeColumnsToContents();
    splitter->addWidget(table);
    splitter->setStretchFactor(1, 1);
    return splitter;
}

QString envelopeText(const QVector<MxEnvelopePoint> &points)
{
    QStringList parts;
    for (const MxEnvelopePoint &p : points)
        parts.append(QStringLiteral("%1 ms→%2%").arg(p.timeMs).arg(p.level * 100 / 0x8000));
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral(", "));
}

QWidget *instrumentsView(const MxMusicFile &file)
{
    auto *table = makeTable({QStringLiteral("Id"), QStringLiteral("Tune"), QStringLiteral("Samples"),
                             QStringLiteral("One-shot / loop"), QStringLiteral("Attack, decay"),
                             QStringLiteral("Release")});
    table->setRowCount(file.instruments().size());
    for (int i = 0; i < file.instruments().size(); ++i) {
        const MxInstrument &ins = file.instruments().at(i);
        QStringList lengths;
        for (const MxSample &s : ins.samples)
            lengths.append(QStringLiteral("%1/%2").arg(s.oneShot).arg(s.loop));
        table->setItem(i, 0, new QTableWidgetItem(QString::number(ins.id)));
        table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1 (%2 semitones)")
                                                      .arg(ins.tune).arg(ins.tune / 24.0, 0, 'f', 2)));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(ins.samples.size())));
        table->setItem(i, 3, new QTableWidgetItem(lengths.join(QStringLiteral(", "))));
        table->setItem(i, 4, new QTableWidgetItem(envelopeText(ins.attack)));
        table->setItem(i, 5, new QTableWidgetItem(envelopeText(ins.release)));
    }
    table->resizeColumnsToContents();
    return table;
}

// One instrument sample at the A440 playback rate of the driver, as a
// separate sound (the loop part repeated a few times).
AudioSample sampleSound(const MxInstrument &ins, int index)
{
    const MxSample &s = ins.samples.at(index);
    AudioSample out;
    out.name = QStringLiteral("Instrument %1 sample %2").arg(ins.id).arg(index + 1);
    out.sampleRate = 16000;
    out.channels = 1;
    out.bits = 16;
    QByteArray body = s.data.left(s.oneShot);
    for (int r = 0; r < 8 && s.loop > 0 && body.size() < 64000; ++r)
        body += s.data.mid(s.oneShot, s.loop);
    out.pcm.resize(body.size() * 2);
    for (int i = 0; i < body.size(); ++i)
        qToLittleEndian<qint16>(static_cast<qint16>(static_cast<qint8>(body.at(i)) * 256), out.pcm.data() + i * 2);
    out.frameCount = body.size();
    out.valid = out.frameCount > 0;
    return out;
}

} // namespace

bool MxtxPlugin::isSatisfiedBy(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return MxMusicFile::looksLikeMx(file.read(16));
}

QWidget *MxtxPlugin::createViewer(const PluginParameters &parameters)
{
    QFile raw(parameters.filename);
    if (!raw.open(QIODevice::ReadOnly))
        return messageView(QStringLiteral("Unable to open the file."));
    const QByteArray data = raw.readAll();
    MxMusicFile file;
    if (!file.parse(data))
        return messageView(QStringLiteral("Unable to read this MXTX music file:\n%1").arg(file.errorString()));

    auto *tabs = new QTabWidget;
    QString summary;
    summary += QStringLiteral("File: %1 (%2 bytes)\n").arg(QFileInfo(parameters.filename).fileName()).arg(data.size());
    summary += QStringLiteral("Songs: %1, instruments: %2, velocity sensitive: %3\n\n")
                   .arg(file.songs().size()).arg(file.instruments().size())
                   .arg(file.velocitySensitive() ? QStringLiteral("yes") : QStringLiteral("no"));
    for (const MxSong &song : file.songs()) {
        QStringList channels;
        for (int c : song.channels)
            channels.append(QString::number(c));
        summary += QStringLiteral("Song %1: %2 notes, %3 BPM, channels %4\n")
                       .arg(song.index + 1).arg(song.notes).arg(song.initialTempo).arg(channels.join(QStringLiteral(", ")));
    }
    summary += QStringLiteral(
        "\nMusic of the Amiga Gold Box games (Secret of the Silver Blades, Pools of\n"
        "Darkness, Dark Queen of Krynn). Each song is a sequence of 6-byte MIDI-like\n"
        "events on 16 channels (channel n plays instrument n), 192 ticks per quarter\n"
        "note. The songs are played with the rules of the game's own driver: four\n"
        "Paula voices (0/3 left, 1/2 right), sampled instruments with a one-shot and a\n"
        "loop part, attack/decay and release envelopes in milliseconds, the sample of\n"
        "the right octave chosen by pitch, PAL timing (50 Hz).");
    auto *summaryView = new QPlainTextEdit(summary);
    summaryView->setReadOnly(true);
    summaryView->setFont(QFont(QStringLiteral("Courier New"), 10));
    tabs->addTab(summaryView, QStringLiteral("Summary"));

    for (const MxSong &song : file.songs())
        tabs->addTab(songView(file, song), QStringLiteral("Song %1").arg(song.index + 1));

    tabs->addTab(instrumentsView(file), QStringLiteral("Instruments"));

    auto *samples = new QTabWidget;
    for (const MxInstrument &ins : file.instruments()) {
        for (int i = 0; i < ins.samples.size(); ++i) {
            const AudioSample sound = sampleSound(ins, i);
            if (sound.valid)
                samples->addTab(new AudioView(sound), ins.samples.size() > 1
                                                           ? QStringLiteral("%1.%2").arg(ins.id).arg(i + 1)
                                                           : QString::number(ins.id));
        }
    }
    tabs->addTab(samples, QStringLiteral("Samples"));
    tabs->addTab(new HexView(data), QStringLiteral("Hex"));
    tabs->setCurrentIndex(file.songs().isEmpty() ? 0 : 1);
    return tabs;
}
