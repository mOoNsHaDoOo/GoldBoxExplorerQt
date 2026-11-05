#include "widgets/audioview.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <QtEndian>

#include <utility>

namespace {

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
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(18, 22, 26));

    const QRect area = rect().adjusted(10, 10, -10, -10);
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
}

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

    layout->addWidget(new WaveformWidget(m_sample, this), 1);

    auto *buttons = new QHBoxLayout;
    auto *playButton = new QPushButton(QStringLiteral("Play"));
    auto *stopButton = new QPushButton(QStringLiteral("Stop"));
    auto *saveButton = new QPushButton(QStringLiteral("Save WAV..."));
    buttons->addWidget(playButton);
    buttons->addWidget(stopButton);
    buttons->addWidget(saveButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color: #666;"));
    layout->addWidget(m_status);

    connect(playButton, &QPushButton::clicked, this, &AudioView::play);
    connect(stopButton, &QPushButton::clicked, this, &AudioView::stop);
    connect(saveButton, &QPushButton::clicked, this, &AudioView::saveWav);
}

AudioView::~AudioView()
{
    stop();
    cleanupTemp();
}

void AudioView::play()
{
    stop();
    if (!m_sample.valid || m_sample.pcm.isEmpty())
        return;

    QTemporaryFile temp(QDir::tempPath() + QStringLiteral("/gbexplorer-XXXXXX.wav"));
    temp.setAutoRemove(false);
    if (!temp.open()) {
        QMessageBox::warning(this, QStringLiteral("Audio"),
                             QStringLiteral("Unable to create a temporary WAV file."));
        return;
    }
    temp.write(m_sample.toWav());
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
        return;
    }

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        m_status->setText(QStringLiteral("Playback finished."));
        m_process->deleteLater();
        m_process = nullptr;
        cleanupTemp();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_status->setText(QStringLiteral("Could not start the audio player."));
    });
    m_status->setText(QStringLiteral("Playing..."));
    m_process->start(program, arguments);
}

void AudioView::stop()
{
    if (!m_process)
        return;
    m_process->kill();
    m_process->waitForFinished(1000);
    m_process->deleteLater();
    m_process = nullptr;
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
