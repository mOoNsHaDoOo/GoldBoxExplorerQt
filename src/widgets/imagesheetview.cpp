#include "widgets/imagesheetview.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QResizeEvent>

#include <algorithm>

ImageSheetView::ImageSheetView(QVector<QImage> images, QVector<int> ids, Options options,
                               QWidget *parent)
    : QWidget(parent)
    , m_images(std::move(images))
    , m_ids(std::move(ids))
    , m_options(options)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    relayout();
}

void ImageSheetView::setZoom(float zoom)
{
    zoom = std::max(0.1f, zoom);
    if (qFuzzyCompare(m_zoom, zoom))
        return;
    m_zoom = zoom;
    relayout();
}

void ImageSheetView::setContainerWidth(int width)
{
    width = std::max(1, width);
    if (m_containerWidth == width)
        return;
    m_containerWidth = width;
    relayout();
}

QSize ImageSheetView::sizeHint() const
{
    return QSize(std::max(1, m_containerWidth), std::max(1, m_contentHeight));
}

void ImageSheetView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (event->size().width() != m_containerWidth) {
        m_containerWidth = std::max(1, event->size().width());
        relayout();
    }
}

void ImageSheetView::relayout()
{
    m_placed.clear();
    m_fontSize = std::max(1, static_cast<int>(14 * m_zoom));
    m_padding = m_options.padding > 0 ? m_options.padding
                                      : std::max(1, m_fontSize * 3);

    qreal x = 0;
    qreal y = m_options.startY;
    qreal rowHeight = 0;
    int imagesInRow = 0;

    for (int i = 0; i < m_images.size(); ++i) {
        const QImage &image = m_images.at(i);
        if (image.isNull())
            continue;

        const qreal width = image.width() * m_zoom;
        const qreal height = image.height() * m_zoom;

        bool newRow = false;
        if (m_options.rowBreakEvery > 0) {
            newRow = imagesInRow >= m_options.rowBreakEvery;
        } else if (m_options.thirtyFivePerRow) {
            newRow = i > 0 && i % 35 == 0;
        } else {
            newRow = x > 0 && x + width > m_containerWidth;
        }

        if (m_options.wallTlb && i > 0) {
            const int m = i % 47;
            if (m == 0 || m == 10 || m == 20 || m == 29 || m == 38)
                newRow = true;
        }

        if (newRow) {
            x = 0;
            const qreal advance = m_options.rowAdvance > 0
                                      ? m_options.rowAdvance * m_zoom
                                      : rowHeight + (m_options.wallTlb
                                                         ? m_padding * m_zoom / 2.0
                                                         : m_padding * m_zoom / 2.0);
            y += advance;
            rowHeight = 0;
            imagesInRow = 0;
        }

        m_placed.append({i, QPointF(x, y), QSizeF(width, height)});
        rowHeight = std::max(rowHeight, height);
        ++imagesInRow;

        if (m_options.rowBreakEvery > 0)
            x += width + m_padding;
        else
            x += (image.width() + m_padding) * m_zoom;
    }

    m_contentHeight = std::max(1, static_cast<int>(y + rowHeight + m_padding / 2));
    updateGeometry();
    update();
}

void ImageSheetView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), palette().window());

    const QFont labelFont(QStringLiteral("Courier New"), m_fontSize);
    painter.setFont(labelFont);
    painter.setPen(QColor(85, 85, 85));

    for (const PlacedImage &placed : std::as_const(m_placed)) {
        const QImage &image = m_images.at(placed.index);
        if (image.isNull())
            continue;

        const QRectF target(placed.position, placed.size);

        // The original viewers use nearest-neighbour scaling so the pixel art
        // stays crisp when zoomed in.
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(target, image);
        painter.restore();

        if (placed.index < m_ids.size())
            painter.drawText(QPointF(target.right() + 2, target.top() + m_fontSize),
                             QString::number(m_ids.at(placed.index)));

        if (m_options.displayBorder) {
            painter.setPen(QColor(Qt::magenta));
            painter.drawRect(target);
            painter.setPen(QColor(85, 85, 85));
        }
    }
}
