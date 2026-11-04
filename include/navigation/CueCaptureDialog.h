#ifndef CUECAPTUREDIALOG_H
#define CUECAPTUREDIALOG_H

#include <QDialog>
#include <QVector>
#include <QString>

class QTableWidget;
class QLabel;
class QPushButton;
class QShowEvent;

// Предъявление команд испытуемому. Собственных часов у диалога нет: этапы и
// обратный отсчёт он получает от таймера проб, по которому сменяется и метка
// в наборе данных, поэтому экран и разметка не расходятся.
class CueCaptureDialog : public QDialog
{
    Q_OBJECT
public:
    struct CueEntry
    {
        QString label;
        QString cueText;
        int totalRepetitions = 0;
    };

    explicit CueCaptureDialog(QWidget *parent = nullptr);
    ~CueCaptureDialog() override;

    void setSchedule(const QVector<CueEntry> &entries,
                     const QVector<QString> &cycleLabels,
                     int prepSeconds, int recordSeconds);

public slots:
    // Проба `cycle` с целевым значением `label` перешла в подготовку или запись.
    void showCue(int cycle, const QString &label, bool recording);
    void showCountdown(double remainingSeconds);
    // Итог проверки последней записанной пробы для оператора: пустая строка
    // убирает отметку; с choice рядом видны кнопки «Повторить» и
    // «Продолжить» --- сбор стоит, пока оператор не выберет.
    void showTrialCheck(const QString &text, bool choice = false);
    // Расписание остановлено после подозрительной пробы.
    void showPaused();
    // В конец расписания добавлена проба с целевым значением label.
    void addTrial(const QString &label);
    void finish(bool stopped);

signals:
    void stopRequested();
    void repeatRequested();
    void continueRequested();

private:
    void fitToText();
    void showEvent(QShowEvent *event) override;
    void updateRemainingTable();
    QString cueTextForLabel(const QString &label) const;

    QLabel *m_phase = nullptr;
    QLabel *m_countdown = nullptr;
    QLabel *m_cycle = nullptr;
    QLabel *m_check = nullptr;
    QPushButton *m_repeat = nullptr;
    QPushButton *m_continue = nullptr;
    QTableWidget *m_table = nullptr;
    QVector<CueEntry> m_entries;
    QVector<QString> m_cycleLabels;
    int m_prepSeconds = 5;
    int m_recordSeconds = 7;
    int m_cycleIndex = 0;
    QString m_currentLabel;
    bool m_recording = false;
};

#endif
