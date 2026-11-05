#include "MainWindow.h"

#include "core/daxfile.h"
#include "parse/audio.h"
#include "parse/smus.h"
#include "parse/slb.h"
#include "widgets/audioview.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugins.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStyleFactory>
#include <QTimer>

#include <typeinfo>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("GoldBoxExplorer"));
    QApplication::setApplicationName(QStringLiteral("GoldBoxExplorerQt"));
    QApplication::setApplicationDisplayName(QStringLiteral("Gold Box Explorer"));
    QApplication::setApplicationVersion(QStringLiteral("0.49"));

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--version")) {
        qInfo().noquote() << QApplication::applicationVersion();
        return 0;
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

