#ifndef CUECAPTUREDIALOG_H
#define CUECAPTUREDIALOG_H

#include <QDialog>
#include <QVector>
#include <QString>
#include <QElapsedTimer>

class QTimer;
class QTableWidget;
class QLabel;
class QShowEvent;

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
    void updateProgress(int labelIndex, int repetition,
                        double seconds, const QString &label);
    void finish(bool stopped);

signals:
    void stopRequested();

private slots:
    void tick();

private:
    void showEvent(QShowEvent *event) override;
    void updateCountdown();
    QString cueTextForLabel(const QString &label) const;

    QLabel *m_phase = nullptr;
    QLabel *m_countdown = nullptr;
    QLabel *m_cycle = nullptr;
    QTableWidget *m_table = nullptr;
    QTimer *m_timer = nullptr;
    QVector<CueEntry> m_entries;
    QVector<QString> m_cycleLabels;
    int m_prepSeconds = 5;
    int m_recordSeconds = 7;
    int m_cycleIndex = 0;
    QString m_currentLabel;
    QElapsedTimer m_elapsed;
    bool m_started = false;
};

#endif
