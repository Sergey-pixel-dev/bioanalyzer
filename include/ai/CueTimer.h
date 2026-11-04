#ifndef CUETIMER_H
#define CUETIMER_H

#include <QElapsedTimer>
#include <QObject>

#include "ai/dataset.h"

class QTimer;

// Таймер проб — граничный объект сбора данных. Задаёт смену предъявляемого
// целевого значения по времени, а не по счётчику принятых отсчётов:
// испытуемый реагирует на команду с экрана, и его задержка на порядок больше
// расхождения таймера со счётчиком. Длительности подготовки и пробы берёт из
// описания задачи.
//
// Это единственные часы сбора: по ним сменяется метка в наборе данных и по
// ним же идёт обратный отсчёт на экране, поэтому экран и разметка не
// расходятся. Работает в потоке интерфейса; саму смену метки поручает
// сеансу сбора (через его очередь команд). Остановленный таймер
// останавливает и сбор: без смены этапа сеанс сбора ничего не записывает.
class CueTimer : public QObject
{
    Q_OBJECT
public:
    explicit CueTimer(QObject *parent = nullptr);

    // Запускает расписание из `cycles` проб: подготовка, затем запись.
    void start(const ParadigmSpec &paradigm, int cycles);
    void stop();
    // Adds `cycles` trials at the end of the schedule; false once stop() has
    // been called. After finished() the added trials start with resume().
    bool extend(int cycles);
    // Пауза: на подготовке --- сразу, на записи --- по её окончании, чтобы
    // проба не оборвалась. После паузы проба начинается заново с подготовки.
    void pause();
    void resume();

signals:
    // Началась подготовка (recording == false) или запись пробы `cycle`.
    void phaseChanged(int cycle, bool recording, int seconds);
    // Секунд до конца текущего этапа; несколько раз в секунду.
    void countdown(double remainingSeconds);
    // Расписание остановлено перед пробой `cycle` (после последней пробы
    // --- `cycle` равен их числу).
    void paused(int cycle);
    // Расписание исчерпано.
    void finished();

private slots:
    void onPhaseEnded();
    void onTick();

private:
    void enterPhase(int cycle, bool recording);
    // Подготовка пробы `cycle` или, за последней пробой, конец расписания.
    void next(int cycle);
    void hold(int cycle);
    double remaining() const;

    QTimer *m_phaseTimer = nullptr;
    QTimer *m_tickTimer = nullptr;
    QElapsedTimer m_phaseClock;
    int m_prepMs = 5000;
    int m_recordMs = 7000;
    int m_cycles = 0;
    int m_cycle = -1;
    bool m_recording = false;
    bool m_paused = false;
    bool m_pauseAfterTrial = false;
};

#endif // CUETIMER_H
