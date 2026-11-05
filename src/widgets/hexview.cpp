#include "widgets/hexview.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

namespace {
constexpr int bytesPerLine = 16;
}

HexView::HexView(QByteArray data, QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_data(std::move(data))
{
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSize(11);
    setFont(m_font);

    const QFontMetrics metrics(m_font);
    m_charWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('0')));
    m_rowHeight = std::max(1, metrics.height() + 2);
    setFocusPolicy(Qt::StrongFocus);
    updateScrollBars();
}

int HexView::offsetDigits() const
{
    int digits = 1;
    quint64 value = static_cast<quint64>(qMax<qint64>(1, m_data.size() - 1));
    while (value >= 16 && digits < 16) {
        value >>= 4;
        ++digits;
    }
    return std::max(6, digits);
}

void HexView::updateScrollBars()
{
    const int rows = (m_data.size() + bytesPerLine - 1) / bytesPerLine;
    const int visibleRows = std::max(1, viewport()->height() / m_rowHeight);

    verticalScrollBar()->setRange(0, std::max(0, rows - 1));
    verticalScrollBar()->setPageStep(visibleRows);
    verticalScrollBar()->setSingleStep(1);

    const int offsetWidth = (offsetDigits() + 2) * m_charWidth;
    const int hexWidth = (bytesPerLine * 3 + 1) * m_charWidth;
    const int asciiWidth = (bytesPerLine + 2) * m_charWidth;
    const int contentWidth = offsetWidth + hexWidth + asciiWidth + 8;
    horizontalScrollBar()->setRange(0, std::max(0, contentWidth - viewport()->width()));
    horizontalScrollBar()->setPageStep(std::max(1, viewport()->width()));
    horizontalScrollBar()->setSingleStep(m_charWidth);
}

void HexView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void HexView::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - steps * 3);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void HexView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().base());
    painter.setFont(m_font);

    const QFontMetrics metrics(m_font);
    const int offsetWidth = (offsetDigits() + 2) * m_charWidth;
    const int hexWidth = (bytesPerLine * 3 + 1) * m_charWidth;
    const int xScroll = horizontalScrollBar()->value();
    const int firstRow = verticalScrollBar()->value();
    const int visibleRows = viewport()->height() / m_rowHeight + 1;

    for (int row = firstRow; row < firstRow + visibleRows; ++row) {
        const qint64 offset = static_cast<qint64>(row) * bytesPerLine;
        if (offset >= m_data.size())
            break;

        const int y = (row - firstRow) * m_rowHeight + metrics.ascent() + 1;

        painter.setPen(QColor(96, 96, 96));
        const QString offsetText = QStringLiteral("%1")
                                       .arg(offset, offsetDigits(), 16, QLatin1Char('0'))
                                       .toUpper();
        painter.drawText(2 - xScroll, y, offsetText + QStringLiteral("  "));

        int x = offsetWidth - xScroll;
        for (int i = 0; i < bytesPerLine; ++i) {
            if (i == 8)
                x += m_charWidth;

            const qint64 index = offset + i;
            if (index >= m_data.size())
                break;

            const quint8 value = static_cast<quint8>(m_data.at(static_cast<int>(index)));
            painter.setPen(QColor(20, 20, 20));
            painter.drawText(x, y, QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());
            x += m_charWidth * 3;
        }

        x = offsetWidth + hexWidth - xScroll;
        painter.setPen(QColor(0, 90, 20));
        QString ascii;
        ascii.reserve(bytesPerLine);
        for (int i = 0; i < bytesPerLine; ++i) {
            const qint64 index = offset + i;
            if (index >= m_data.size())
                break;
            const quint8 value = static_cast<quint8>(m_data.at(static_cast<int>(index)));
            ascii.append(value >= 32 && value <= 126 ? QChar(value) : QLatin1Char('.'));
        }
        painter.drawText(x + m_charWidth, y, ascii);
    }
}
