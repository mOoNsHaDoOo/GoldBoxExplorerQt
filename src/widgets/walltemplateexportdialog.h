#pragma once

#include <QDialog>
#include <QImage>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;

// Specialised wall/overlay template exporter modelled on the original
// WallTemplateExportForm.  It composites the ten pre-rendered wall views of
// one wallset into a template image, optionally marking the pixels that also
// appear in a second wallset (overlay mode).
class WallTemplateExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WallTemplateExportDialog(const QVector<QImage> &walls,
                                      QWidget *parent = nullptr);

    QImage templateImage() const;

private:
    void chooseTemplate();
    void updatePreview();
    void saveTemplate();
    QImage buildTemplate() const;

    QVector<QImage> m_walls;
    QImage m_template;
    QLabel *m_preview = nullptr;
    QLabel *m_templateLabel = nullptr;
    QComboBox *m_wallCombo = nullptr;
    QCheckBox *m_overlay = nullptr;
    QComboBox *m_overlayCombo = nullptr;
};
