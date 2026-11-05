#pragma once

#include "parse/audio.h"

#include <QWidget>

class QLabel;
class QProcess;

class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(const AudioSample &sample, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const AudioSample &m_sample;
};

class AudioView : public QWidget
{
    Q_OBJECT
public:
    explicit AudioView(AudioSample sample, QWidget *parent = nullptr);
    ~AudioView() override;

private:
    void play();
    void stop();
    void saveWav();
    void cleanupTemp();

    AudioSample m_sample;
    QLabel *m_status = nullptr;
    QProcess *m_process = nullptr;
    QString m_tempWav;
};
