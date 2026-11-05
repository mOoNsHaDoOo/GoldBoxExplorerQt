#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

struct PluginParameters
{
    QString filename;
    float zoom = 1.0f;
    int containerWidth = 800;
};

class Plugin
{
public:
    virtual ~Plugin() = default;

    virtual bool isSatisfiedBy(const QString &path) const = 0;
    virtual QWidget *createViewer(const PluginParameters &parameters) = 0;

    virtual void setZoom(float zoom) { Q_UNUSED(zoom); }
    virtual void setContainerWidth(int width) { Q_UNUSED(width); }

    virtual bool isImageFile() const { return false; }
    virtual QVector<QImage> getBitmaps() { return {}; }
    virtual QVector<int> getBitmapIds() { return {}; }
};
