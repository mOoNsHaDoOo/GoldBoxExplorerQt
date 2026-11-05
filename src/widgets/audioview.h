#pragma once

#include "parse/audio.h"

#include <QWidget>
#include <QPointer>

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

    void startPlaybackForTesting() { play(); }

private:
    void play();
    void stop();
    void saveWav();
    void cleanupTemp();

    AudioSample m_sample;
    QLabel *m_status = nullptr;
    QPointer<QProcess> m_process;
    QString m_tempWav;
};
