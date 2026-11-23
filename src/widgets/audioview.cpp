#include "widgets/audioview.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>

#include <utility>

namespace {

QString clockText(qint64 milliseconds)
{
    return QStringLiteral("%1:%2.%3")
        .arg(milliseconds / 60000)
        .arg((milliseconds / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg((milliseconds % 1000) / 100);
}

QString formatDuration(qint64 milliseconds)
{
    if (milliseconds < 1000)
        return QStringLiteral("%1 ms").arg(milliseconds);
    return QStringLiteral("%1.%2 s")
        .arg(milliseconds / 1000)
        .arg((milliseconds % 1000) / 10, 2, 10, QLatin1Char('0'));
}

} // namespace

WaveformWidget::WaveformWidget(const AudioSample &sample, QWidget *parent)
    : QWidget(parent)
    , m_sample(sample)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::IBeamCursor);
    setToolTip(QStringLiteral("Click or drag to move the play position"));
}

QRect WaveformWidget::waveArea() const
{
    return rect().adjusted(10, 10, -10, -10);
}

qint64 WaveformWidget::frameCount() const
{
    return m_sample.pcm.size() / (2 * qMax(1, m_sample.channels));
}

qint64 WaveformWidget::frameAt(int x) const
{
    const QRect area = waveArea();
    if (area.width() <= 0)
        return 0;
    const qint64 frames = frameCount();
    return qBound<qint64>(0, (static_cast<qint64>(x - area.left()) * frames) / area.width(), qMax<qint64>(0, frames - 1));
}

void WaveformWidget::setPosition(qint64 frame)
{
    if (frame == m_position)
        return;
    m_position = frame;
    update();
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    setPosition(frameAt(qRound(event->position().x())));
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
        setPosition(frameAt(qRound(event->position().x())));
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    setPosition(frameAt(qRound(event->position().x())));
    emit seekRequested(m_position);
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(18, 22, 26));

    const QRect area = waveArea();
    painter.setPen(QColor(50, 60, 70));
    painter.drawLine(area.left(), area.center().y(), area.right(), area.center().y());

    if (m_sample.pcm.size() < 2 || area.width() <= 0)
        return;

    const int sampleCount = m_sample.pcm.size() / 2;
    painter.setPen(QColor(80, 220, 140));

    for (int x = 0; x < area.width(); ++x) {
        const int start = static_cast<int>((static_cast<qint64>(x) * sampleCount) / area.width());
        const int end = static_cast<int>((static_cast<qint64>(x + 1) * sampleCount) / area.width());
        qint16 low = 32767;
        qint16 high = -32768;
        for (int i = start; i < end && i < sampleCount; ++i) {
            const qint16 value = qFromLittleEndian<qint16>(m_sample.pcm.constData() + i * 2);
            low = qMin(low, value);
            high = qMax(high, value);
        }
        if (high < low)
            continue;
        const int top = area.center().y() - (static_cast<int>(high) * area.height()) / 65536;
        const int bottom = area.center().y() - (static_cast<int>(low) * area.height()) / 65536;
        painter.drawLine(area.left() + x, top, area.left() + x, bottom);
    }

    // the play position
    const qint64 frames = frameCount();
    if (frames > 0) {
        const int x = area.left() + static_cast<int>((m_position * area.width()) / frames);
        painter.setPen(QPen(QColor(255, 210, 60), 2));
        painter.drawLine(x, rect().top(), x, rect().bottom());
    }
}

QPointer<AudioView> AudioView::s_playing;

AudioView::AudioView(AudioSample sample, QWidget *parent)
    : QWidget(parent)
    , m_sample(std::move(sample))
{
    auto *layout = new QVBoxLayout(this);

    const QString title = m_sample.name.isEmpty()
                              ? QStringLiteral("Amiga IFF audio")
                              : m_sample.name;
    auto *heading = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    layout->addWidget(heading);

    auto *info = new QLabel(QStringLiteral("%1 Hz · %2-bit · %3 channel(s) · %4")
                                .arg(m_sample.sampleRate)
                                .arg(m_sample.bits)
                                .arg(m_sample.channels)
                                .arg(formatDuration(m_sample.durationMs())));
    layout->addWidget(info);

    m_wave = new WaveformWidget(m_sample, this);
    layout->addWidget(m_wave, 1);
    connect(m_wave, &WaveformWidget::seekRequested, this, &AudioView::seek);

    auto *buttons = new QHBoxLayout;
    auto *playButton = new QPushButton(QStringLiteral("Play"));
    auto *stopButton = new QPushButton(QStringLiteral("Stop"));
    auto *saveButton = new QPushButton(QStringLiteral("Save WAV..."));
    m_time = new QLabel;
    m_time->setFont(QFont(QStringLiteral("Courier New"), 10));
    buttons->addWidget(playButton);
    buttons->addWidget(stopButton);
    buttons->addWidget(saveButton);
    buttons->addSpacing(12);
    buttons->addWidget(m_time);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color: #666;"));
    layout->addWidget(m_status);

    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, &AudioView::tick);

    connect(playButton, &QPushButton::clicked, this, &AudioView::play);
    connect(stopButton, &QPushButton::clicked, this, &AudioView::stop);
    connect(saveButton, &QPushButton::clicked, this, &AudioView::saveWav);
    updateTimeLabel();
}

AudioView::~AudioView()
{
    stop();
    cleanupTemp();
}

void AudioView::stopAll()
{
    if (s_playing)
        s_playing->stop();
}

qint64 AudioView::currentFrame() const
{
    if (!m_process || m_sample.sampleRate <= 0)
        return m_wave->position();
    const qint64 frames = m_sample.pcm.size() / (2 * qMax(1, m_sample.channels));
    return qMin(frames, m_startFrame + m_clock.elapsed() * m_sample.sampleRate / 1000);
}

void AudioView::updateTimeLabel()
{
    if (!m_time)
        return;
    const qint64 rate = qMax(1, m_sample.sampleRate);
    m_time->setText(QStringLiteral("%1 / %2")
                        .arg(clockText(m_wave->position() * 1000 / rate))
                        .arg(clockText(m_sample.durationMs())));
}

void AudioView::tick()
{
    if (!m_wave->dragging())
        m_wave->setPosition(currentFrame());
    updateTimeLabel();
}

void AudioView::seek(qint64 frame)
{
    const bool playing = m_process;
    if (playing)
        stop();
    m_wave->setPosition(frame);
    updateTimeLabel();
    if (playing)
        play();   // go on playing from the new position
}

void AudioView::play()
{
    // one sound at a time
    if (s_playing && s_playing != this)
        s_playing->stop();
    const qint64 from = m_process ? currentFrame() : m_wave->position();
    stop();
    if (!m_sample.valid || m_sample.pcm.isEmpty())
        return;

    const int bytesPerFrame = 2 * qMax(1, m_sample.channels);
    const qint64 frames = m_sample.pcm.size() / bytesPerFrame;
    m_startFrame = (from >= frames - 1) ? 0 : qMax<qint64>(0, from);
    AudioSample part = m_sample;
    part.pcm = m_sample.pcm.mid(m_startFrame * bytesPerFrame);
    part.frameCount = static_cast<int>(part.pcm.size() / bytesPerFrame);

    QTemporaryFile temp(QDir::tempPath() + QStringLiteral("/gbexplorer-XXXXXX.wav"));
    temp.setAutoRemove(false);
    if (!temp.open()) {
        QMessageBox::warning(this, QStringLiteral("Audio"),
                             QStringLiteral("Unable to create a temporary WAV file."));
        return;
    }
    temp.write(part.toWav());
    m_tempWav = temp.fileName();
    temp.close();

    QString program;
    QStringList arguments;

#ifdef Q_OS_MACOS
    program = QStringLiteral("/usr/bin/afplay");
    arguments << m_tempWav;
#elif defined(Q_OS_WIN)
    program = QStringLiteral("powershell");
    QString escaped = m_tempWav;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    arguments << QStringLiteral("-NoProfile") << QStringLiteral("-Command")
              << QStringLiteral("(New-Object Media.SoundPlayer '%1').PlaySync()").arg(escaped);
#else
    const QString aplay = QStandardPaths::findExecutable(QStringLiteral("aplay"));
    const QString paplay = QStandardPaths::findExecutable(QStringLiteral("paplay"));
    const QString ffplay = QStandardPaths::findExecutable(QStringLiteral("ffplay"));
    if (!aplay.isEmpty()) {
        program = aplay;
        arguments << QStringLiteral("-q") << m_tempWav;
    } else if (!paplay.isEmpty()) {
        program = paplay;
        arguments << m_tempWav;
    } else if (!ffplay.isEmpty()) {
        program = ffplay;
        arguments << QStringLiteral("-nodisp") << QStringLiteral("-autoexit")
                  << QStringLiteral("-loglevel") << QStringLiteral("quiet") << m_tempWav;
    }
#endif

    if (program.isEmpty()) {
        m_status->setText(QStringLiteral("No audio player found. Use “Save WAV...” and open it externally."));
        cleanupTemp();
        return;
    }

    QProcess *process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        if (m_process != process)
            return;   // stopped or replaced by a new play
        m_process = nullptr;
        process->deleteLater();
        m_timer->stop();
        if (s_playing == this)
            s_playing = nullptr;
        m_wave->setPosition(0);   // played to the end: back to the start
        updateTimeLabel();
        if (m_status)
            m_status->setText(QStringLiteral("Playback finished."));
        cleanupTemp();
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_status)
            m_status->setText(QStringLiteral("Could not start the audio player."));
    });
    m_status->setText(QStringLiteral("Playing..."));
    s_playing = this;
    m_clock.start();
    m_timer->start();
    process->start(program, arguments);
    tick();
}

void AudioView::stop()
{
    QProcess *process = m_process.data();
    if (!process)
        return;
    const qint64 at = currentFrame();
    m_process = nullptr;
    m_timer->stop();
    if (s_playing == this)
        s_playing = nullptr;

    process->kill();
    process->waitForFinished(1000);
    process->deleteLater();
    m_wave->setPosition(at);   // Play goes on from here
    updateTimeLabel();
    if (m_status)
        m_status->setText(QStringLiteral("Stopped."));
    cleanupTemp();
}

void AudioView::saveWav()
{
    if (!m_sample.valid)
        return;

    QString base = m_sample.name;
    if (base.isEmpty())
        base = QStringLiteral("sound");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save WAV"), base + QStringLiteral(".wav"),
        QStringLiteral("WAV audio (*.wav)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Audio"),
                             QStringLiteral("Unable to write %1").arg(path));
        return;
    }
    file.write(m_sample.toWav());
    m_status->setText(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
}

void AudioView::cleanupTemp()
{
    if (m_tempWav.isEmpty())
        return;
    QFile::remove(m_tempWav);
    m_tempWav.clear();
}
