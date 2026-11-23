#include "widgets/tabordering.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QGraphicsView>
#include <QLabel>
#include <QRegularExpression>
#include <QTabBar>
#include <QTabWidget>
#include <QVector>

namespace {

// Widgets that draw pictures, maps or sounds (by class name: some are local
// to their plugin).
bool drawsSomething(const QWidget *w)
{
    static const char *const classes[] = {
        "AudioView", "WaveformWidget", "GeoFirstPersonView", "ImageSheetView", "QGraphicsView",
    };
    for (const char *name : classes)
        if (w->inherits(name))
            return true;
    if (w->property("gbVisual").toBool())   // set by widgets without a meta class
        return true;
    if (const auto *label = qobject_cast<const QLabel *>(w))
        return !label->pixmap().isNull();
    return false;
}

} // namespace

bool VisualTabsFirst::isVisualPage(QWidget *page, const QString &title)
{
    if (!page)
        return false;
    // titles of pages that are filled when opened (songs, samples...)
    static const QRegularExpression visualTitle(
        QStringLiteral("\\b(song|music|sample|samples|sound|sounds|audio|image|images|picture|pictures|"
                       "view|map|maps|first person|piano|frames?|preview|tiles?|sprites?|animation|walls?|"
                       "icons?|font|palette|portrait|bitmaps?)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression textTitle(
        QStringLiteral("\\b(hex|info|summary|songs|table|tables|text|strings|events|tracks|disassembly|listing|"
                       "fields|records|hunks|instruments)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (textTitle.match(title).hasMatch())
        return false;
    if (visualTitle.match(title).hasMatch())
        return true;
    if (drawsSomething(page))
        return true;
    const QList<QWidget *> children = page->findChildren<QWidget *>();
    for (const QWidget *child : children)
        if (drawsSomething(child))
            return true;
    return false;
}

void VisualTabsFirst::reorder(QTabWidget *tabs)
{
    if (!tabs || tabs->count() < 2)
        return;
    QWidget *current = tabs->currentWidget();
    QVector<QWidget *> visual;
    for (int i = 0; i < tabs->count(); ++i)
        if (isVisualPage(tabs->widget(i), tabs->tabText(i)))
            visual.append(tabs->widget(i));
    if (visual.isEmpty() || visual.size() == tabs->count())
        return;
    int target = 0;
    for (QWidget *page : visual) {
        const int from = tabs->indexOf(page);
        if (from != target)
            tabs->tabBar()->moveTab(from, target);
        ++target;
    }
    if (current && !visual.contains(current))
        tabs->setCurrentIndex(0);
    else if (current)
        tabs->setCurrentWidget(current);
}

bool VisualTabsFirst::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show) {
        if (auto *tabs = qobject_cast<QTabWidget *>(watched)) {
            if (!tabs->property("gbVisualFirst").toBool()) {
                tabs->setProperty("gbVisualFirst", true);
                reorder(tabs);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
