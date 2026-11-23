#pragma once

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QPlainTextEdit>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;

// Read-only text view for ECL Basic listings: Ctrl+click on a label or table
// name jumps to its definition, hovering a [variable] shows its description.
class EclBasicEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit EclBasicEdit(QWidget *parent = nullptr);

    void setDefinitions(const QHash<QString, int> &definitions) { m_definitions = definitions; }
    bool jumpTo(const QString &name);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    QString wordAt(const QPoint &pos) const;
    QString variableAt(const QPoint &pos) const;

    QHash<QString, int> m_definitions; // label/table name -> block number
};

// One ECL Basic listing with a search bar and a label list.  When the decode
// failed, the view explains that the block is not in any known dialect
// (Champions of Krynn, Death Knights of Krynn, Pool of Radiance).
class EclBasicView : public QWidget
{
    Q_OBJECT

public:
    EclBasicView(bool ok, const QString &text, const QString &error, QWidget *parent = nullptr);

    QString text() const;

private:
    void find(bool backwards);

    EclBasicEdit *m_edit = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_status = nullptr;
    QListWidget *m_labels = nullptr;
};

// The ECL Basic listings of several blocks of a container (for example a GLB
// with ECL data): a block list on the left with the decode status, the
// selected listing on the right.  Each block is tried as a raw ECL block
// (2-byte prefix, as in ECLn.DAX) and then as a bare program image.
class EclBasicBlocksView : public QWidget
{
    Q_OBJECT

public:
    EclBasicBlocksView(const QVector<QPair<int, QByteArray>> &blocks, const QString &fileTag,
                       QWidget *parent = nullptr);

    int decodedCount() const { return m_decoded; }

private:
    QListWidget *m_list = nullptr;
    QStackedWidget *m_stack = nullptr;
    int m_decoded = 0;
};
