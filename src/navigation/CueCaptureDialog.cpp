#include "navigation/CueCaptureDialog.h"
#include <QAbstractItemView>

#include "ui_cuecapturedialog.h"

#include <QApplication>
#include <QScreen>
#include <QLayout>
#include <QFontMetrics>
#include <QHeaderView>
#include <QShowEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
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
    m_check = form.checkLabel;
    m_check->hide();
    m_repeat = form.repeatButton;
    m_repeat->hide();
    connect(m_repeat, &QPushButton::clicked, this, &CueCaptureDialog::repeatRequested);
    m_continue = form.continueButton;
    m_continue->hide();
    connect(m_continue, &QPushButton::clicked, this, &CueCaptureDialog::continueRequested);
    m_table = form.remainingTable;
    // Таблица только показывает оставшиеся пробы.
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Длинная метка видна целиком: колонка жестов занимает всю ширину.
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
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
    m_recording = false;
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

void CueCaptureDialog::showCue(int cycle, const QString &label, bool recording)
{
    if (cycle >= 0 && cycle < m_cycleLabels.size())
        m_cycleIndex = cycle;
    m_currentLabel = label.isEmpty() && m_cycleIndex < m_cycleLabels.size()
                         ? m_cycleLabels[m_cycleIndex]
                         : label;
    m_recording = recording;
    const QString cue = m_currentLabel.isEmpty() ? QStringLiteral("-") : cueTextForLabel(m_currentLabel);
    m_phase->setText(recording ? tr("Perform: %1").arg(cue)
                               : tr("Preparation: %1").arg(cue));
    m_cycle->setText(tr("Cycle %1 of %2").arg(m_cycleIndex + 1).arg(m_cycleLabels.size()));
    updateRemainingTable();
    if (isVisible())
        fitToText();
}

void CueCaptureDialog::fitToText()
{
    // Окно подстраивается под команду: ширина — по длине текста, но не больше
    // 80 % экрана; длиннее текст переносится, и окно растёт в высоту.
    QScreen *screen = this->screen() ? this->screen() : QApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    m_phase->ensurePolished();
    const int textWidth = QFontMetrics(m_phase->font()).horizontalAdvance(m_phase->text()) +
                          layout()->contentsMargins().left() + layout()->contentsMargins().right() + 24;
    setFixedWidth(std::clamp(textWidth, 420, area.width() * 8 / 10));
    adjustSize();
    move(area.center() - rect().center());
}

void CueCaptureDialog::showTrialCheck(const QString &text, bool choice)
{
    // Отметка мелкая и в стороне от команды: она для оператора, а испытуемый
    // смотрит на команду и обратный отсчёт.
    m_check->setText(text);
    m_check->setVisible(!text.isEmpty());
    m_repeat->setVisible(choice);
    m_continue->setVisible(choice);
}

void CueCaptureDialog::showPaused()
{
    m_phase->setText(tr("Paused"));
    m_countdown->setText(QStringLiteral("-"));
    if (isVisible())
        fitToText();
}

void CueCaptureDialog::addTrial(const QString &label)
{
    m_cycleLabels.push_back(label);
    for (auto &entry : m_entries)
        if (entry.label == label)
            ++entry.totalRepetitions;
    m_cycle->setText(tr("Cycle %1 of %2").arg(m_cycleIndex + 1).arg(m_cycleLabels.size()));
    updateRemainingTable();
}

void CueCaptureDialog::showCountdown(double remainingSeconds)
{
    m_countdown->setText(QString::number(std::max(1, int(std::ceil(remainingSeconds)))));
}

void CueCaptureDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    fitToText();
}

QString CueCaptureDialog::cueTextForLabel(const QString &label) const
{
    for (const auto &entry : m_entries)
        if (entry.label == label)
            return entry.cueText.isEmpty() ? entry.label : entry.cueText;
    return label;
}

void CueCaptureDialog::updateRemainingTable()
{
    // Во время записи текущая проба уже считается выполненной.
    const int done = m_cycleIndex + (m_recording ? 1 : 0);
    for (int row = 0; row < m_entries.size(); ++row)
    {
        int completed = 0;
        for (int i = 0; i < done && i < m_cycleLabels.size(); ++i)
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
    close();
}
