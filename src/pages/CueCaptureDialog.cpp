#include "pages/CueCaptureDialog.h"

#include "ui_cuecapturedialog.h"

#include <QApplication>
#include <QScreen>
#include <QShowEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

CueCaptureDialog::CueCaptureDialog(QWidget *parent)
    : QDialog(parent)
{
    Ui::CueCaptureDialogForm form;
    form.setupUi(this);
    m_phase = form.phaseLabel;
    m_countdown = form.countdownLabel;
    m_cycle = form.cycleLabel;
    m_table = form.remainingTable;
    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &CueCaptureDialog::tick);
    connect(form.stopButton, &QPushButton::clicked, this, &CueCaptureDialog::stopRequested);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setModal(false);
}

CueCaptureDialog::~CueCaptureDialog() = default;

void CueCaptureDialog::setSchedule(const QVector<CueEntry> &entries,
                                   const QVector<QString> &cycleLabels,
                                   int prepSeconds, int recordSeconds)
{
    m_entries = entries;
    m_cycleLabels = cycleLabels;
    m_prepSeconds = std::max(0, prepSeconds);
    m_recordSeconds = std::max(1, recordSeconds);
    m_cycleIndex = 0;
    m_currentLabel.clear();
    m_started = false;
    m_table->setRowCount(m_entries.size());
    for (int row = 0; row < m_entries.size(); ++row)
    {
        m_table->setItem(row, 0, new QTableWidgetItem(m_entries[row].label));
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(m_entries[row].totalRepetitions)));
    }
    m_cycle->setText(tr("Cycle 0 of %1").arg(m_cycleLabels.size()));
    m_phase->setText(tr("Waiting"));
    m_countdown->setText(QStringLiteral("-"));
}

void CueCaptureDialog::updateProgress(int labelIndex, int repetition,
                                      double seconds, const QString &label)
{
    Q_UNUSED(repetition);
    if (labelIndex >= 0 && labelIndex < m_cycleLabels.size())
        m_cycleIndex = labelIndex;
    m_currentLabel = label.isEmpty() && m_cycleIndex < m_cycleLabels.size()
                         ? m_cycleLabels[m_cycleIndex]
                         : label;
    if (seconds == 0.0 || !m_started)
    {
        m_elapsed.restart();
        m_started = true;
        if (!m_timer->isActive())
            m_timer->start();
    }
    updateCountdown();
}

void CueCaptureDialog::tick()
{
    updateCountdown();
}

void CueCaptureDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    adjustSize();
    if (auto *screen = QApplication::screenAt(QCursor::pos()))
    {
        const QRect area = screen->availableGeometry();
        move(area.center() - rect().center());
    }
    else if (auto *screen = QApplication::primaryScreen())
    {
        const QRect area = screen->availableGeometry();
        move(area.center() - rect().center());
    }
}

QString CueCaptureDialog::cueTextForLabel(const QString &label) const
{
    for (const auto &entry : m_entries)
        if (entry.label == label)
            return entry.cueText.isEmpty() ? entry.label : entry.cueText;
    return label;
}

void CueCaptureDialog::updateCountdown()
{
    if (!m_started)
        return;
    const double elapsed = m_elapsed.isValid() ? m_elapsed.elapsed() / 1000.0 : 0.0;
    const bool preparation = elapsed < double(m_prepSeconds);
    const QString cue = m_currentLabel.isEmpty() ? QStringLiteral("-") : cueTextForLabel(m_currentLabel);
    const int phaseDuration = preparation ? m_prepSeconds : m_recordSeconds;
    const double phaseElapsed = preparation ? elapsed : elapsed - double(m_prepSeconds);
    const int remaining = std::max(1, int(std::ceil(double(phaseDuration) - phaseElapsed)));
    m_phase->setText(preparation ? tr("Preparation: %1").arg(cue)
                                 : tr("Perform: %1").arg(cue));
    m_countdown->setText(QString::number(remaining));
    m_cycle->setText(tr("Cycle %1 of %2").arg(m_cycleIndex + 1).arg(m_cycleLabels.size()));

    for (int row = 0; row < m_entries.size(); ++row)
    {
        int completed = 0;
        for (int i = 0; i < m_cycleIndex && i < m_cycleLabels.size(); ++i)
            if (m_cycleLabels[i] == m_entries[row].label)
                ++completed;
        const int remainingRepetitions = std::max(0, m_entries[row].totalRepetitions - completed);
        if (!m_table->item(row, 1))
            m_table->setItem(row, 1, new QTableWidgetItem);
        m_table->item(row, 1)->setText(QString::number(remainingRepetitions));
    }
}

void CueCaptureDialog::finish(bool stopped)
{
    Q_UNUSED(stopped);
    if (m_timer)
        m_timer->stop();
    close();
}
