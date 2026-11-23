#include "MainWindow.h"

#include "core/container.h"
#include "core/daxfile.h"
#include "core/adf.h"
#include "core/tempfolder.h"
#include "core/zip.h"
#include "parse/audio.h"
#include "parse/smus.h"
#include "parse/slb.h"
#include "parse/mxmusic.h"
#include "parse/exemusic.h"
#include "parse/ecl.h"
#include "parse/ecl_disasm.h"
#include "parse/ecl_basic.h"
#include "dax/walldef.h"
#include "widgets/walltemplateexportdialog.h"
#include "widgets/audioview.h"
#include "widgets/geofpview.h"
#include "plugins/containerplugin.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugins.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <memory>

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
    QApplication::setApplicationVersion(QStringLiteral("0.98"));

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--version")) {
        qInfo().noquote() << QApplication::applicationVersion();
        return 0;
    }

    // Lists the files of an Amiga disk image: --adf FILE [OUTDIR] (with
    // OUTDIR the files are also extracted there).
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--adf")) {
        AdfImage image;
        if (!image.load(QString::fromLocal8Bit(argv[2]))) {
            qWarning().noquote() << image.errorString();
            return 1;
        }
        qInfo().noquote() << QStringLiteral("%1 volume \"%2\", %3 blocks, %4 entries")
                                 .arg(image.fileSystem(), image.volumeName())
                                 .arg(image.blockCount()).arg(image.entries().size());
        for (const AdfEntry &e : image.entries())
            qInfo().noquote() << (e.isDirectory ? QStringLiteral("%1/").arg(e.path)
                                                : QStringLiteral("%1  %2").arg(e.path).arg(e.size));
        if (argc >= 4 && !image.extractTo(QString::fromLocal8Bit(argv[3]))) {
            qWarning().noquote() << image.errorString();
            return 1;
        }
        for (const QString &w : image.warnings())
            qWarning().noquote() << "warning:" << w;
        return 0;
    }

    // Lists the entries of a ZIP archive: --zip FILE [OUTDIR] (with OUTDIR
    // the entries are also extracted there).
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--zip")) {
        ZipArchive zip;
        if (!zip.load(QString::fromLocal8Bit(argv[2]))) {
            qWarning().noquote() << zip.errorString();
            return 1;
        }
        for (const ZipEntry &e : zip.entries())
            qInfo().noquote() << (e.isDirectory ? QStringLiteral("%1/").arg(e.path)
                                                : QStringLiteral("%1  %2  method %3").arg(e.path).arg(e.size).arg(e.method));
        if (argc >= 4)
            zip.extractTo(QString::fromLocal8Bit(argv[3]));
        for (const QString &w : zip.warnings())
            qWarning().noquote() << "warning:" << w;
        return zip.warnings().isEmpty() ? 0 : 1;
    }

    // Renders one song of an MXTX music file: --mx-render FILE SONG OUT.wav
    // (SONG from 1; without OUT only the song list is printed).
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--mx-render")) {
        MxMusicFile mx;
        if (!mx.load(QString::fromLocal8Bit(argv[2]))) {
            qWarning().noquote() << mx.errorString();
            return 1;
        }
        for (const MxSong &song : mx.songs())
            qInfo().noquote() << QStringLiteral("song %1: %2 events, %3 notes, tempo %4, channels %5")
                                     .arg(song.index + 1).arg(song.events.size()).arg(song.notes)
                                     .arg(song.initialTempo)
                                     .arg([&] { QStringList c; for (int x : song.channels) c << QString::number(x); return c.join(QLatin1Char(',')); }());
        if (argc >= 5) {
            const AudioSample out = mx.render(QString::fromLocal8Bit(argv[3]).toInt() - 1);
            QFile file(QString::fromLocal8Bit(argv[4]));
            if (!out.valid || !file.open(QIODevice::WriteOnly)) {
                qWarning().noquote() << "render failed" << out.error;
                return 1;
            }
            file.write(out.toWav());
            qInfo().noquote() << QStringLiteral("%1 ms").arg(out.durationMs());
        }
        return 0;
    }

    // Plays the music built into an Amiga executable (Pool of Radiance
    // `program`): --exe-music FILE [SONG OUT.wav] (SONG from 1).
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--exe-music")) {
        QFile in(QString::fromLocal8Bit(argv[2]));
        ExeMusic music;
        if (!in.open(QIODevice::ReadOnly) || !music.load(in.readAll())) {
            qWarning().noquote() << "no music:" << music.errorString();
            return 1;
        }
        qInfo().noquote() << music.playerName();
        for (int i = 0; i < music.songNames().size(); ++i)
            qInfo().noquote() << QStringLiteral("song %1: %2").arg(i + 1).arg(music.songNames().at(i));
        if (argc >= 5) {
            const AudioSample out = music.render(QString::fromLocal8Bit(argv[3]).toInt() - 1);
            QFile file(QString::fromLocal8Bit(argv[4]));
            if (!out.valid || !file.open(QIODevice::WriteOnly)) {
                qWarning().noquote() << "render failed" << out.error;
                return 1;
            }
            file.write(out.toWav());
            qInfo().noquote() << QStringLiteral("%1 ms").arg(out.durationMs());
        }
        return 0;
    }

    // First-person view of a GEO map after some moves:
    // --fp GEOFILE MAP MOVES OUT.png (MAP from 0; MOVES: F B L R l r, "-" for none).
    if (argc >= 6 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--fp")) {
        std::unique_ptr<Plugin> plugin = PluginFactory::createForFile(QString::fromLocal8Bit(argv[2]));
        PluginParameters parameters;
        parameters.filename = QString::fromLocal8Bit(argv[2]);
        QWidget *viewer = plugin ? plugin->createViewer(parameters) : nullptr;
        // the views in the order of the map menu (the pages of their stack)
        QList<GeoFirstPersonView *> views;
        if (viewer) {
            if (auto *first = viewer->findChild<GeoFirstPersonView *>()) {
                if (auto *stack = qobject_cast<QStackedWidget *>(first->parentWidget())) {
                    for (int i = 0; i < stack->count(); ++i)
                        if (auto *view = qobject_cast<GeoFirstPersonView *>(stack->widget(i)))
                            views.append(view);
                }
            }
        }
        const int map = QString::fromLocal8Bit(argv[3]).toInt();
        if (map < 0 || map >= views.size()) {
            qWarning().noquote() << "no first-person view" << map << "of" << views.size();
            return 1;
        }
        GeoFirstPersonView *view = views.at(map);
        const QString moves = QString::fromLocal8Bit(argv[4]);
        if (moves != QStringLiteral("-"))
            view->applyMoves(moves);
        qInfo().noquote() << QStringLiteral("x %1 y %2 facing %3").arg(view->cursorX()).arg(view->cursorY()).arg(view->facing());
        return view->viewImage().save(QString::fromLocal8Bit(argv[5])) ? 0 : 1;
    }

    // Per-frame state of the Paula voices of an MX song:
    // --mx-trace FILE SONG FRAMES (SONG from 1); one line per frame,
    // "frame | period volume cycles" for each voice.
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--mx-trace")) {
        MxMusicFile mx;
        if (!mx.load(QString::fromLocal8Bit(argv[2]))) {
            qWarning().noquote() << mx.errorString();
            return 1;
        }
        const auto rows = mx.trace(QString::fromLocal8Bit(argv[3]).toInt() - 1, QString::fromLocal8Bit(argv[4]).toInt());
        for (int f = 0; f < rows.size(); ++f) {
            std::printf("%d", f);
            for (const MxMusicFile::VoiceFrame &v : rows.at(f))
                std::printf(" | %d %d %d", v.cycles < 0 ? 0 : v.period, v.cycles < 0 ? 0 : v.volume, v.cycles);
            std::printf("\n");
        }
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

    // Lists the songs of an SLB song bank: --slb FILE [SONG OUT.wav] (SONG from 1).
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--slb")) {
        SlbFile file(QString::fromLocal8Bit(argv[2]));
        if (!file.isValid()) {
            qWarning().noquote() << file.errorString();
            return 1;
        }
        qInfo().noquote() << "SLB songs:" << file.songs().size();
        for (const SlbSong &song : file.songs())
            qInfo().noquote() << QStringLiteral("  song %1: tempo %2 (factor %3), volume %4, %5 tracks, %6 notes")
                                     .arg(song.index + 1).arg(song.tempo).arg(song.tempoFactor())
                                     .arg(song.volume).arg(song.tracks.size()).arg(song.noteCount());
        if (argc >= 5) {
            const int index = QString::fromLocal8Bit(argv[3]).toInt() - 1;
            if (index < 0 || index >= file.songs().size()) {
                qWarning().noquote() << "no such song";
                return 1;
            }
            const AudioSample sample = SlbFile::render(file.songs().at(index));
            QFile output(QString::fromLocal8Bit(argv[4]));
            if (!sample.valid || !output.open(QIODevice::WriteOnly)) {
                qWarning().noquote() << "render failed" << sample.error;
                return 1;
            }
            output.write(sample.toWav());
            qInfo().noquote() << QStringLiteral("%1 ms").arg(sample.durationMs());
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

    // Temporary folder for the extracted disk images: emptied now, deleted
    // when main returns.
    TempFolder::Scope tempFolder;
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

