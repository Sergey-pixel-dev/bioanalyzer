#include "navigation/PlaybackBar.h"

#include "navigation/AppContext.h"
#include "device/qtdevicesessionadapter.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProxyStyle>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

namespace
{
    // Makes a left-click anywhere on a slider's groove jump the handle to that
    // spot AND start a drag from there — the YouTube-style scrub behaviour.
    // (By default Qt only page-steps toward the click.)
    class AbsoluteSeekStyle : public QProxyStyle
    {
    public:
        using QProxyStyle::QProxyStyle;
        int styleHint(StyleHint hint, const QStyleOption *option,
                      const QWidget *widget, QStyleHintReturn *ret) const override
        {
            if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
                return Qt::LeftButton | Qt::MiddleButton;
            return QProxyStyle::styleHint(hint, option, widget, ret);
        }
    };

    constexpr int kSliderSteps = 1000;

    QString formatTime(quint64 samples, int sampleRate)
    {
        const quint64 seconds = samples / static_cast<quint64>(qMax(1, sampleRate));
        const quint64 h = seconds / 3600;
        const quint64 m = (seconds / 60) % 60;
        const quint64 s = seconds % 60;
        if (h > 0)
            return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
        return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    }
}

PlaybackBar::PlaybackBar(AppContext *context, QWidget *parent)
    : QWidget(parent), m_context(context)
{
    // [ тело панели | кнопка свёртывания ]. Свёртывание скрывает только тело:
    // кнопка остаётся на своём месте справа и меняет лишь направление стрелки.
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(6, 2, 6, 2);
    root->setSpacing(6);

    m_body = new QWidget(this);
    auto *row = new QHBoxLayout(m_body);
    row->setContentsMargins(0, 0, 0, 0);

    m_toggleButton = new QToolButton(m_body);
    m_toggleButton->setAutoRaise(true);
    m_backButton = new QToolButton(m_body);
    m_backButton->setText(tr("−1 s"));
    m_backButton->setToolTip(tr("Back one second"));
    m_backButton->setAutoRaise(true);
    m_forwardButton = new QToolButton(m_body);
    m_forwardButton->setText(tr("+1 s"));
    m_forwardButton->setToolTip(tr("Forward one second"));
    m_forwardButton->setAutoRaise(true);

    m_slider = new QSlider(Qt::Horizontal, m_body);
    m_slider->setRange(0, kSliderSteps);
    // The proxy style is parented to the slider so it lives as long.
    m_slider->setStyle(new AbsoluteSeekStyle(m_slider->style()));

    m_timeLabel = new QLabel(m_body);
    m_timeLabel->setMinimumWidth(m_timeLabel->fontMetrics().horizontalAdvance(QStringLiteral("00:00:00 / 00:00:00")));
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    row->addWidget(m_toggleButton);
    row->addWidget(m_backButton);
    row->addWidget(m_forwardButton);
    row->addWidget(m_slider, 1);
    row->addWidget(m_timeLabel);
    root->addWidget(m_body, 1);
    // В свёрнутом виде тело скрыто; растяжка держит кнопку у правого края.
    root->addStretch(0);

    m_collapseButton = new QToolButton(this);
    m_collapseButton->setAutoRaise(true);
    root->addWidget(m_collapseButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    connect(m_toggleButton, &QToolButton::clicked, this, &PlaybackBar::toggleStream);
    connect(m_backButton, &QToolButton::clicked, this, [this] { step(-1); });
    connect(m_forwardButton, &QToolButton::clicked, this, [this] { step(1); });
    connect(m_collapseButton, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });

    // YouTube-style scrubbing: while the handle is held, every move seeks and
    // the recording keeps playing from the new place; position reports do
    // not move the handle under the user's hand.
    connect(m_slider, &QSlider::sliderPressed, this, [this] { m_sliderHeld = true; });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int value)
            {
                seekTo(sliderToSample(value));
                updateTimeLabel(sliderToSample(value)); });
    connect(m_slider, &QSlider::sliderReleased, this, [this]
            {
                m_sliderHeld = false;
                seekTo(sliderToSample(m_slider->value())); });

    if (m_context)
    {
        connect(m_context, &AppContext::sessionChanged, this, &PlaybackBar::onSessionChanged);
        onSessionChanged(m_context->session());
    }
    else
    {
        onSessionChanged(nullptr);
    }
    setExpanded(true);
}

void PlaybackBar::setExpanded(bool expanded)
{
    const bool changed = m_expanded != expanded;
    m_expanded = expanded;
    m_body->setVisible(expanded);
    m_collapseButton->setArrowType(expanded ? Qt::DownArrow : Qt::UpArrow);
    m_collapseButton->setToolTip(expanded ? tr("Hide the playback bar") : tr("Show the playback bar"));
    if (changed)
        emit expandedChanged(expanded);
}

void PlaybackBar::onSessionChanged(QtDeviceSessionAdapter *session)
{
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    m_position = 0;
    m_total = 0;
    m_sampleRate = 1;
    m_streaming = false;

    if (m_session)
    {
        connect(m_session, &QtDeviceSessionAdapter::streamingChanged,
                this, &PlaybackBar::onStreamingChanged);
        connect(m_session, &QtDeviceSessionAdapter::playbackPositionChanged,
                this, &PlaybackBar::onPosition);
        connect(m_session, &QtDeviceSessionAdapter::connectionChanged,
                this, [this](bool) { updateVisibility(); });
        // The adapter is replaced before it is deleted; forget it with it.
        connect(m_session, &QObject::destroyed, this, [this](QObject *object)
                {
                    if (object == m_session)
                        onSessionChanged(nullptr); });
        if (m_session->isPlayback())
        {
            m_total = m_session->playbackTotalSamples();
            m_sampleRate = qMax(1, m_session->playbackSampleRate());
        }
        m_streaming = m_session->isStreaming();
    }
    onStreamingChanged(m_streaming);
    updateTimeLabel(0);
    m_slider->setValue(0);
    updateVisibility();
}

void PlaybackBar::updateVisibility()
{
    // The bar belongs to the virtual device and to nothing else.
    setVisible(m_session && m_session->isPlayback() && m_session->isConnected());
}

void PlaybackBar::onStreamingChanged(bool streaming)
{
    m_streaming = streaming;
    m_toggleButton->setIcon(style()->standardIcon(streaming ? QStyle::SP_MediaStop
                                                            : QStyle::SP_MediaPlay));
    m_toggleButton->setToolTip(streaming ? tr("Stop the stream") : tr("Start the stream"));
}

void PlaybackBar::onPosition(quint64 position, quint64 total)
{
    m_position = position;
    m_total = total;
    if (m_sliderHeld)
        return;
    updateTimeLabel(position);
    if (total > 0)
    {
        const int value = static_cast<int>(
            (static_cast<double>(position) / static_cast<double>(total)) * kSliderSteps);
        QSignalBlocker block(m_slider);
        m_slider->setValue(value);
    }
}

void PlaybackBar::toggleStream()
{
    if (!m_session)
        return;
    // The same adapter calls as the device page's stream button.
    if (m_streaming)
        m_session->stopStream();
    else
        m_session->startStream();
}

void PlaybackBar::step(int seconds)
{
    if (m_total == 0)
        return;
    // One second forward or back, around the loop in both directions.
    const qint64 total = static_cast<qint64>(m_total);
    qint64 target = static_cast<qint64>(m_position) + static_cast<qint64>(seconds) * m_sampleRate;
    target %= total;
    if (target < 0)
        target += total;
    seekTo(static_cast<quint64>(target));
    onPosition(static_cast<quint64>(target), m_total);
}

quint64 PlaybackBar::sliderToSample(int value) const
{
    if (m_total == 0)
        return 0;
    const double fraction = static_cast<double>(value) / kSliderSteps;
    return qMin<quint64>(static_cast<quint64>(fraction * static_cast<double>(m_total)), m_total - 1);
}

void PlaybackBar::seekTo(quint64 sample)
{
    if (!m_session)
        return;
    m_position = sample;
    m_session->seekToSample(sample);
}

void PlaybackBar::updateTimeLabel(quint64 position)
{
    m_timeLabel->setText(QStringLiteral("%1 / %2")
                             .arg(formatTime(position, m_sampleRate))
                             .arg(formatTime(m_total, m_sampleRate)));
}
