#include "ai/CueTimer.h"

#include <QTimer>

#include <algorithm>

CueTimer::CueTimer(QObject *parent)
    : QObject(parent)
{
    m_phaseTimer = new QTimer(this);
    m_phaseTimer->setSingleShot(true);
    m_phaseTimer->setTimerType(Qt::PreciseTimer);
    connect(m_phaseTimer, &QTimer::timeout, this, &CueTimer::onPhaseEnded);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(250);
    connect(m_tickTimer, &QTimer::timeout, this, &CueTimer::onTick);
}

void CueTimer::start(const ParadigmSpec &paradigm, int cycles)
{
    stop();
    if (cycles <= 0)
        return;
    m_prepMs = std::max(0, paradigm.prepSeconds) * 1000;
    m_recordMs = std::max(1, paradigm.recordSeconds) * 1000;
    m_cycles = cycles;
    next(0);
}

void CueTimer::stop()
{
    m_phaseTimer->stop();
    m_tickTimer->stop();
    m_cycle = -1;
    m_recording = false;
    m_paused = false;
    m_pauseAfterTrial = false;
}

bool CueTimer::extend(int cycles)
{
    if (m_cycle < 0 || cycles <= 0)
        return false;
    m_cycles += cycles;
    return true;
}

void CueTimer::pause()
{
    if (m_cycle < 0 || m_paused)
        return;
    if (m_recording)
        m_pauseAfterTrial = true;
    else
        hold(m_cycle);
}

void CueTimer::resume()
{
    m_pauseAfterTrial = false;
    if (m_cycle < 0 || !m_paused)
        return;
    m_paused = false;
    next(m_cycle);
}

void CueTimer::hold(int cycle)
{
    m_phaseTimer->stop();
    m_tickTimer->stop();
    m_cycle = cycle;
    m_recording = false;
    m_paused = true;
    m_pauseAfterTrial = false;
    emit paused(cycle);
}

void CueTimer::next(int cycle)
{
    if (cycle < m_cycles)
    {
        m_tickTimer->start();
        enterPhase(cycle, false);
        return;
    }
    // Расписание исчерпано, но до stop() его ещё можно продлить: так
    // повторяется последняя проба.
    m_phaseTimer->stop();
    m_tickTimer->stop();
    m_cycle = cycle;
    m_recording = false;
    emit finished();
}

void CueTimer::enterPhase(int cycle, bool recording)
{
    m_cycle = cycle;
    m_recording = recording;
    const int ms = recording ? m_recordMs : m_prepMs;
    m_phaseClock.start();
    m_phaseTimer->start(ms);
    emit phaseChanged(cycle, recording, ms / 1000);
    emit countdown(remaining());
}

void CueTimer::onPhaseEnded()
{
    if (m_cycle < 0)
        return;
    if (!m_recording)
    {
        enterPhase(m_cycle, true);
        return;
    }
    if (m_pauseAfterTrial)
        hold(m_cycle + 1);
    else
        next(m_cycle + 1);
}

double CueTimer::remaining() const
{
    const int ms = m_recording ? m_recordMs : m_prepMs;
    return std::max(0.0, (ms - m_phaseClock.elapsed()) / 1000.0);
}

void CueTimer::onTick()
{
    if (m_cycle >= 0 && !m_paused)
        emit countdown(remaining());
}
