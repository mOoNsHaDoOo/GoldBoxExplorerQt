#pragma once

#include <QImage>
#include <QVector>
#include <QWidget>

// Lays out a list of images in rows, with the same wrapping/zoom behaviour as
// the WinForms PictureBox based viewers in the original application.
class ImageSheetView : public QWidget
{
    Q_OBJECT
public:
    struct Options
    {
        bool thirtyFivePerRow = false;
        bool displayBorder = false;
        bool wallTlb = false;
        int rowBreakEvery = 0;   // 0 = wrap based on width
        int rowAdvance = 0;      // 0 = use tallest image in the row
        int padding = 0;         // 0 = three times the font size
        int startY = 0;
    };

    ImageSheetView(QVector<QImage> images, QVector<int> ids, Options options,
                   QWidget *parent = nullptr);

    void setZoom(float zoom);
    float zoom() const { return m_zoom; }
    void setContainerWidth(int width);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {1, 1}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout();

    struct PlacedImage
    {
        int index = 0;
        QPointF position;
        QSizeF size;
    };

    QVector<QImage> m_images;
    QVector<int> m_ids;
    QVector<PlacedImage> m_placed;
    Options m_options;
    float m_zoom = 1.0f;
    int m_containerWidth = 800;
    int m_contentHeight = 1;
    int m_fontSize = 14;
    int m_padding = 42;
};
