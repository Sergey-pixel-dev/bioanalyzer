#ifndef AI_INFERENCEPAGE_H
#define AI_INFERENCEPAGE_H

#include "navigation/AiPageBase.h"

class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class InferenceSession;

// Граничный класс варианта использования «Распознавать жесты в реальном
// времени»: принимает действия оператора, передаёт их управляющему объекту
// InferenceSession и отображает полученные результаты. Логики сценария и
// работы с потоком отсчётов не содержит.
class InferencePage : public AiPageBase
{
    Q_OBJECT
public:
    explicit InferencePage(AppContext *context, QWidget *parent = nullptr);

private slots:
    void chooseModel();
    void start();
    void stop();
    void onStrideChanged(int samples);

private:
    void applyRunningState(bool running);

    InferenceSession *m_session = nullptr;
    QLineEdit *m_model = nullptr;
    QLabel *m_descriptor = nullptr;
    QTableWidget *m_top5 = nullptr;
    QSpinBox *m_inferenceStride = nullptr;
    QDoubleSpinBox *m_smoothing = nullptr;
    // The gesture shown: the smoothed result with hysteresis.
    QLabel *m_result = nullptr;
    QPushButton *m_modelBrowse = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
};

#endif // AI_INFERENCEPAGE_H
