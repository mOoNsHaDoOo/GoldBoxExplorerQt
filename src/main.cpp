#include "MainWindow.h"

#include "plugins/pluginfactory.h"
#include "plugins/plugins.h"

#include <QApplication>
#include <QDebug>
#include <QStyleFactory>
#include <QTimer>

#include <typeinfo>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("GoldBoxExplorer"));
    QApplication::setApplicationName(QStringLiteral("GoldBoxExplorerQt"));
    QApplication::setApplicationDisplayName(QStringLiteral("Gold Box Explorer"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Small diagnostic mode used by the smoke tests: print what the plugin
    // layer can decode without opening a window.
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--probe")) {
        const QString path = QString::fromLocal8Bit(argv[2]);
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

