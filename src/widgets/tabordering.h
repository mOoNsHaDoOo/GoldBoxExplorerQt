#pragma once

#include <QObject>

class QTabWidget;
class QWidget;

// Puts the tabs that show something (pictures, maps, the first-person view,
// sounds and music) before the others (summaries, tables, text, hex) in
// every tab widget of the viewers, the first time it is shown. Installed on
// the application (event filter), so viewers built later or lazily are
// covered too. Keeps the selected tab unless it is a text one and a visual
// tab exists, then the first visual tab is selected.
class VisualTabsFirst : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    static bool isVisualPage(QWidget *page, const QString &title);
    static void reorder(QTabWidget *tabs);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
