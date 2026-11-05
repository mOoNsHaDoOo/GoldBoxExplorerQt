#include "MainWindow.h"

#include "core/daxfile.h"
#include "parse/audio.h"
#include "plugins/pluginfactory.h"
#include "plugins/plugins.h"

#include <QApplication>
#include <QDebug>
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
    QApplication::setApplicationVersion(QStringLiteral("0.41"));

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

