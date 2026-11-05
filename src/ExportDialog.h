#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QComboBox;
class Plugin;

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    ExportDialog(Plugin *plugin, const QString &sourceFile, QWidget *parent = nullptr);

private:
    void browse();
    void exportImages();

    Plugin *m_plugin = nullptr;
    QString m_sourceFile;
    QLineEdit *m_folderEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
};
