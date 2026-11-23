#pragma once

#include "parse/audio.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>

class QLabel;
class QProcess;
class QTimer;

// The waveform of a sound with a play position: a vertical line that moves
// while the sound plays; clicking or dragging moves it (seekRequested when
// the mouse button is released).
class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(const AudioSample &sample, QWidget *parent = nullptr);

    void setPosition(qint64 frame);
    qint64 position() const { return m_position; }
    bool dragging() const { return m_dragging; }

signals:
    void seekRequested(qint64 frame);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect waveArea() const;
    qint64 frameCount() const;
    qint64 frameAt(int x) const;

    const AudioSample &m_sample;
    qint64 m_position = 0;
    bool m_dragging = false;
};

// A playable sound: waveform with a play position, Play / Stop / Save WAV.
// Only one AudioView plays at a time: Play stops the one that is playing.
// Play starts from the play position; Stop leaves the position where the
// sound stopped; at the end it goes back to the start.
class AudioView : public QWidget
{
    Q_OBJECT
public:
    explicit AudioView(AudioSample sample, QWidget *parent = nullptr);
    ~AudioView() override;

    void startPlaybackForTesting() { play(); }

    // Stops whatever AudioView is playing.
    static void stopAll();

private:
    void play();
    void stop();
    void seek(qint64 frame);
    void tick();
    void saveWav();
    void cleanupTemp();
    qint64 currentFrame() const;
    void updateTimeLabel();

    AudioSample m_sample;
    WaveformWidget *m_wave = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_time = nullptr;
    QTimer *m_timer = nullptr;
    QPointer<QProcess> m_process;
    QString m_tempWav;
    QElapsedTimer m_clock;
    qint64 m_startFrame = 0;

    static QPointer<AudioView> s_playing;
};
