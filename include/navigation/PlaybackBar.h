#ifndef PLAYBACKBAR_H
#define PLAYBACKBAR_H

#include <QWidget>

// A complete type: a slot takes a pointer to it, and moc's meta-type code
// needs the full definition.
#include "device/qtdevicesessionadapter.h"

class AppContext;
class QToolButton;
class QSlider;
class QLabel;

// Playback bar of the virtual device.
//
// Sits at the bottom of the main window under every page and is visible only
// while the current device is the virtual one. It is a device control, like
// the device page: its start/stop button issues the same adapter calls as the
// page's stream button, and both show the same state (streamingChanged), so
// they never disagree. Seeking — by the timeline or by one second back and
// forth — goes to the virtual device through the session's command queue and
// is allowed while the recording plays.
//
// The bar can be collapsed: its body hides while the collapse button stays
// where it was. The collapsed state belongs to the main window's presentation
// model.
class PlaybackBar : public QWidget
{
    Q_OBJECT
public:
    explicit PlaybackBar(AppContext *context, QWidget *parent = nullptr);

    bool isExpanded() const { return m_expanded; }

public slots:
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);

private slots:
    void onSessionChanged(QtDeviceSessionAdapter *session);
    void onStreamingChanged(bool streaming);
    void onPosition(quint64 position, quint64 total);
    void toggleStream();
    void step(int seconds);

private:
    void updateVisibility();
    void updateTimeLabel(quint64 position);
    quint64 sliderToSample(int value) const;
    void seekTo(quint64 sample);

    AppContext *m_context = nullptr;
    QtDeviceSessionAdapter *m_session = nullptr;

    QWidget *m_body = nullptr; // everything but the collapse button
    QToolButton *m_toggleButton = nullptr;
    QToolButton *m_backButton = nullptr;
    QToolButton *m_forwardButton = nullptr;
    // Stays in place in both states; only its arrow changes.
    QToolButton *m_collapseButton = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_timeLabel = nullptr;

    quint64 m_position = 0;
    quint64 m_total = 0;
    int m_sampleRate = 1;
    bool m_streaming = false;
    bool m_sliderHeld = false;
    bool m_expanded = true;
};

#endif // PLAYBACKBAR_H
