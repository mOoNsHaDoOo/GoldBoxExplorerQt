#include "MainWindow.h"

#include "core/container.h"
#include "core/daxfile.h"
#include "parse/audio.h"
#include "parse/smus.h"
#include "parse/slb.h"
#include "parse/mxtx.h"
#include "parse/ecl.h"
#include "parse/ecl_disasm.h"
#include "parse/ecl_basic.h"
#include "dax/walldef.h"
#include "widgets/walltemplateexportdialog.h"
#include "widgets/audioview.h"
#include "plugins/containerplugin.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugins.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>
#include <cstring>

#include <typeinfo>

namespace {

// --ecl-basic FILE [BLOCK]: prints the ECL Basic listing of every block (or
// of one block) of a Champions of Krynn ECLn.DAX to stdout.  Runs before
// QApplication so it needs no display.
int printEclBasic(const QString &path, int onlyBlock)
{
    DaxFile file(path);
    if (!file.isValid()) {
        std::fprintf(stderr, "%s: %s\n", qPrintable(path), qPrintable(file.errorString()));
        return 1;
    }
    const QString tag = eclBasicFileTag(path);
    int failures = 0;
    bool found = false;
    for (const DaxFileBlock &block : file.blocks()) {
        if (onlyBlock >= 0 && block.id != onlyBlock)
            continue;
        found = true;
        const EclBasicResult result = eclBasicDisassembleBlock(block.data, tag, block.id);
        if (!result.ok) {
            std::fprintf(stderr, "%s block %d: not ECL Basic: %s\n", qPrintable(tag), block.id,
                         qPrintable(result.error));
            ++failures;
            continue;
        }
        const QByteArray bytes = result.text.toLatin1();
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    }
    if (!found) {
        std::fprintf(stderr, "%s: no block %d\n", qPrintable(path), onlyBlock);
        return 2;
    }
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc >= 3 && std::strcmp(argv[1], "--ecl-basic") == 0) {
        bool ok = true;
        const int block = argc >= 4 ? QString::fromLocal8Bit(argv[3]).toInt(&ok) : -1;
        if (!ok) {
            std::fprintf(stderr, "usage: %s --ecl-basic FILE [BLOCK]\n", argv[0]);
            return 2;
        }
        return printEclBasic(QString::fromLocal8Bit(argv[1 + 1]), block);
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("GoldBoxExplorer"));
    QApplication::setApplicationName(QStringLiteral("GoldBoxExplorerQt"));
    QApplication::setApplicationDisplayName(QStringLiteral("Gold Box Explorer"));
    QApplication::setApplicationVersion(QStringLiteral("0.57"));

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--version")) {
        qInfo().noquote() << QApplication::applicationVersion();
        return 0;
    }

    // Lists the inner blocks of a multi-block file (DAX, DAA, GLB, TLB).
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--list")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        Container container;
        if (!container.open(path)) {
            qWarning().noquote() << path << "is not a recognised container";
            return 1;
        }
        qInfo().noquote() << path;
        qInfo().noquote() << "format:" << container.formatName();
        qInfo().noquote() << container.summary();
        qInfo().noquote() << QStringLiteral("%1 %2 %3 %4 %5  %6")
                                 .arg(QStringLiteral("#"), 4)
                                 .arg(QStringLiteral("id"), 5)
                                 .arg(QStringLiteral("offset"), 8)
                                 .arg(QStringLiteral("stored"), 7)
                                 .arg(QStringLiteral("size"), 7)
                                 .arg(QStringLiteral("type"));
        for (const ContainerEntry &e : container.entries()) {
            qInfo().noquote() << QStringLiteral("%1 %2 %3 %4 %5  %6%7")
                                     .arg(e.index, 4)
                                     .arg(e.id, 5)
                                     .arg(e.offset < 0 ? QStringLiteral("-") : QString::number(e.offset), 8)
                                     .arg(e.offset < 0 ? QStringLiteral("-") : QString::number(e.storedSize), 7)
                                     .arg(e.offset < 0 ? QStringLiteral("-") : QString::number(e.data.size()), 7)
                                     .arg(e.type)
                                     .arg(e.decoded ? QString() : QStringLiteral(" [not decoded]"));
        }
        return 0;
    }

    // Opens one inner block of a container the way the block browser does
    // and reports the plugin that shows it: --probe-block FILE ROW (ROW 1 =
    // first block). Fails when the block falls back to the hex view only.
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--probe-block")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        const int row = QString::fromLocal8Bit(argv[3]).toInt();
        Container container;
        if (!container.open(path) || row < 1 || row > container.entries().size()) {
            qWarning().noquote() << "no block" << row << "in" << path;
            return 2;
        }
        ContainerPlugin plugin(container);
        PluginParameters parameters;
        parameters.filename = path;
        QWidget *viewer = plugin.createViewer(parameters);
        plugin.selectRow(row);
        const ContainerEntry &entry = container.entries().at(row - 1);
        const QString viewerName = plugin.entryViewerName();
        qInfo().noquote() << path << "block" << entry.id << entry.type
                          << "viewer:" << (viewerName.isEmpty() ? QStringLiteral("hex") : viewerName)
                          << "images:" << plugin.getBitmaps().size();
        delete viewer;
        return viewerName.isEmpty() ? 1 : 0;
    }

    // Small diagnostic mode used by the smoke tests: print what the plugin
    // layer can decode without opening a window.
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--probe")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        if (path.endsWith(QStringLiteral(".dax"), Qt::CaseInsensitive)
            || path.endsWith(QStringLiteral(".DAX"), Qt::CaseInsensitive)) {
            DaxFile probeDax(path);
            qInfo().noquote() << "DAX valid:" << probeDax.isValid()
                              << "amiga:" << probeDax.isAmiga()
                              << "blocks:" << probeDax.blocks().size();
        }
        auto plugin = PluginFactory::create(path);
        if (!plugin) {
            qWarning() << "no plugin for" << path;
            return 2;
        }
        PluginParameters parameters;
        parameters.filename = path;
        parameters.zoom = 1.0f;
        parameters.containerWidth = 800;
        QWidget *viewer = plugin->createViewer(parameters);
        const QVector<QImage> bitmaps = plugin->getBitmaps();
        const Plugin &pluginRef = *plugin;
        qInfo().noquote() << path << "plugin:" << typeid(pluginRef).name();
        qInfo().noquote() << path << "images:" << bitmaps.size()
                          << "image-file:" << plugin->isImageFile();
        for (int i = 0; i < bitmaps.size(); ++i)
            qInfo().noquote() << "  " << i << bitmaps[i].width() << "x" << bitmaps[i].height();
        delete viewer;
        return bitmaps.isEmpty() ? 1 : 0;
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--playtest")) {
        QFile input(QString::fromLocal8Bit(argv[2]));
        AudioSample sample;
        QString error;
        if (!input.open(QIODevice::ReadOnly)
            || !load8svx(input.readAll(), sample, &error)) {
            qWarning().noquote() << error;
            return 1;
        }
        auto *view = new AudioView(sample);
        view->show();
        view->startPlaybackForTesting();
        QTimer::singleShot(300, view, [view] {
            delete view;
            qApp->quit();
        });
        return app.exec();
    }

    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--walltemplate")) {
        DaxWallDefFile file(QString::fromLocal8Bit(argv[2]));
        if (!file.isValid() || file.wallSets().isEmpty()) {
            qWarning() << "Unable to read a wall set";
            return 1;
        }
        WallTemplateExportDialog dialog(file.wallSets().first().walls);
        const QString output = QString::fromLocal8Bit(argv[3]);
        if (!dialog.templateImage().save(output)) {
            qWarning() << "Unable to save" << output;
            return 1;
        }
        qInfo().noquote() << "wrote" << output;
        return 0;
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--ecl")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        EclFile file(path);
        if (!file.isValid()) {
            qWarning().noquote() << file.errorString();
            return 1;
        }
        const bool pool = path.contains(QStringLiteral("pool"), Qt::CaseInsensitive);
        for (const EclBlock &block : file.blocks()) {
            const EclDisasmResult result = disassembleEclBlock(block.data, pool);
            qInfo().noquote() << QStringLiteral("block %1: %2 instructions, %3 bytes")
                                     .arg(block.id)
                                     .arg(result.lines.size())
                                     .arg(result.listing.size());
            QFile output(QStringLiteral("/tmp/ecl_disasm_%1.txt").arg(block.id));
            if (output.open(QIODevice::WriteOnly))
                output.write(result.listing.toUtf8());
        }
        return 0;
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--mxtx")) {
        MxtxFile file(QString::fromLocal8Bit(argv[2]));
        if (!file.isValid()) {
            qWarning().noquote() << file.errorString();
            return 1;
        }
        qInfo().noquote() << "MXTX:" << file.fileSize() << "bytes, tempo" << file.tempo()
                          << "events" << file.events().size() << "ticks" << file.totalTicks()
                          << "sequence" << QStringLiteral("0x%1..0x%2")
                                 .arg(file.eventStart(), 0, 16)
                                 .arg(file.eventEnd(), 0, 16)
                          << "waveforms" << file.waveforms().size()
                          << "at" << QStringLiteral("0x%1").arg(file.waveformStart(), 0, 16);
        const AudioSample sample = MxtxFile::synthesize(file);
        if (!sample.valid) {
            qWarning() << "MXTX synthesis failed";
            return 1;
        }
        qInfo().noquote() << "duration" << sample.durationMs() << "ms";
        QFile output(QStringLiteral("/tmp/gbexplorer_mxtx.wav"));
        if (output.open(QIODevice::WriteOnly))
            output.write(sample.toWav());
        return 0;
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--slb")) {
        SlbFile file(QString::fromLocal8Bit(argv[2]));
        if (!file.isValid()) {
            qWarning().noquote() << file.errorString();
            return 1;
        }
        qInfo().noquote() << "SLB songs:" << file.songs().size();
        for (const SlbSong &song : file.songs()) {
            const AudioSample sample = SlbFile::synthesize(song);
            qInfo().noquote() << QStringLiteral("  song %1: 0x%2..0x%3 loop 0x%4 events %5 notes %6 units %7 duration %8 ms")
                                     .arg(song.index + 1)
                                     .arg(song.start, 4, 16, QLatin1Char('0'))
                                     .arg(song.end, 4, 16, QLatin1Char('0'))
                                     .arg(song.loopPoint, 4, 16, QLatin1Char('0'))
                                     .arg(song.events.size())
                                     .arg(song.noteCount)
                                     .arg(song.totalUnits)
                                     .arg(sample.durationMs());
        }
        if (!file.songs().isEmpty()) {
            const AudioSample sample = SlbFile::synthesize(file.songs().first());
            QFile output(QStringLiteral("/tmp/gbexplorer_slb.wav"));
            if (output.open(QIODevice::WriteOnly))
                output.write(sample.toWav());
            qInfo().noquote() << "wrote /tmp/gbexplorer_slb.wav";
        }
        return 0;
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--smus")) {
        QFile input(QString::fromLocal8Bit(argv[2]));
        if (!input.open(QIODevice::ReadOnly))
            return 2;
        SmusScore score;
        QString error;
        if (!loadSmus(input.readAll(), score, &error)) {
            qWarning().noquote() << error;
            return 1;
        }
        const QFileInfo info(QString::fromLocal8Bit(argv[2]));
        SmusBank bank;
        const QStringList entries = info.absoluteDir().entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            if (entry.compare(QStringLiteral("soundData"), Qt::CaseInsensitive) == 0) {
                QFile bankFile(info.absoluteDir().filePath(entry));
                if (bankFile.open(QIODevice::ReadOnly))
                    loadSmusBank(bankFile.readAll(), bank);
                break;
            }
        }
        const AudioSample music = synthesizeSmus(score, bank);
        if (!music.valid) {
            qWarning() << "SMUS synthesis failed";
            return 1;
        }
        qInfo().noquote() << "SMUS:" << score.name << "tracks:" << score.tracks.size()
                          << "tempo:" << score.tempo << "samples:" << bank.samples.size()
                          << "instruments:" << bank.instrumentSamples.size()
                          << "duration:" << music.durationMs() << "ms";
        for (auto it = score.instrumentNames.cbegin(); it != score.instrumentNames.cend(); ++it) {
            qInfo().noquote() << "  register" << it.key() << "=" << it.value()
                              << "->" << bank.instrumentSamples.value(it.value().toLower());
        }
        QFile output(QStringLiteral("/tmp/gbexplorer_smus.wav"));
        if (output.open(QIODevice::WriteOnly))
            output.write(music.toWav());
        return 0;
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--audio")) {
        QFile input(QString::fromLocal8Bit(argv[2]));
        if (!input.open(QIODevice::ReadOnly))
            return 2;
        AudioSample sample;
        QString error;
        if (!load8svx(input.readAll(), sample, &error)) {
            qWarning().noquote() << error;
            return 1;
        }
        qInfo().noquote() << "audio:" << sample.sampleRate << "Hz" << sample.bits << "bit"
                          << sample.frameCount << "frames" << sample.durationMs() << "ms";
        QFile output(QStringLiteral("/tmp/gbexplorer_audio.wav"));
        if (output.open(QIODevice::WriteOnly)) {
            output.write(sample.toWav());
            output.close();
            qInfo().noquote() << "wrote /tmp/gbexplorer_audio.wav";
        }
        return 0;
    }

    MainWindow window;
    window.show();

    // Screenshot helper: --shot FILE ROW OUTPUT.png [WIDTH HEIGHT]
    // opens FILE, selects ROW of the block browser (0 = all blocks) and
    // saves a grab of the window.
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--shot")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        const int row = QString::fromLocal8Bit(argv[3]).toInt();
        const QString output = QString::fromLocal8Bit(argv[4]);
        if (argc >= 7)
            window.resize(QString::fromLocal8Bit(argv[5]).toInt(), QString::fromLocal8Bit(argv[6]).toInt());
        QTimer::singleShot(200, &window, [&window, path, row] {
            window.openForTesting(path);
            if (row > 0 && !window.selectContainerRowForTesting(row))
                qWarning() << "not a container:" << path;
        });
        QTimer::singleShot(1500, &window, [&window, output] {
            window.grab().save(output);
            qApp->quit();
        });
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--smoke")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        QTimer::singleShot(200, &window, [&window, path] { window.openForTesting(path); });
        QTimer::singleShot(1200, &window, [&window] {
            window.grab().save(QStringLiteral("/tmp/gbexplorer_smoke.png"));
            qApp->quit();
        });
    }

    return app.exec();
}

