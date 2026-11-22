#include "core/tempfolder.h"

#include <QCoreApplication>
#include <QDir>

namespace TempFolder {

namespace {

QString g_path;

QString programFolder()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tmp"));
}

QString fallbackFolder()
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("GoldBoxExplorerQt-tmp"));
}

} // namespace

QString path()
{
    return g_path.isEmpty() ? programFolder() : g_path;
}

bool reset()
{
    remove();
    for (const QString &candidate : {programFolder(), fallbackFolder()}) {
        QDir dir(candidate);
        if (dir.exists())
            dir.removeRecursively();
        if (QDir().mkpath(candidate)) {
            g_path = candidate;
            return true;
        }
    }
    return false;
}

void remove()
{
    if (g_path.isEmpty())
        return;
    QDir dir(g_path);
    if (dir.exists())
        dir.removeRecursively();
}

} // namespace TempFolder
