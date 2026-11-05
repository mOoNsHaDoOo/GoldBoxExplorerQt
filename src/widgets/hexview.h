#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFont>

class QWheelEvent;

// A compact hex viewer modelled on the Be.HexEditor control used by the
// original WinForms application.
class HexView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit HexView(QByteArray data, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateScrollBars();
    int offsetDigits() const;

    QByteArray m_data;
    QFont m_font;
    int m_charWidth = 8;
    int m_rowHeight = 16;
};
