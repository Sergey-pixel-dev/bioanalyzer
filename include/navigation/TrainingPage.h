#ifndef AI_TRAININGPAGE_H
#define AI_TRAININGPAGE_H

#include "navigation/AiPageBase.h"
#include "ai/aimodel.h"

#include <QHash>
#include <QJsonObject>
#include <QVector>

class QLineEdit;
class QComboBox;
class QPushButton;
class QFormLayout;
class QTableWidget;
class QCustomPlot;
class QCPGraph;
class QSplitter;
class TrainingSession;

// Граничный класс варианта использования «Обучить модель распознавания»:
// собирает параметры, передаёт их управляющему объекту TrainingSession и
// отображает ход обучения.
//
// Папка модели выбирается так же, как папка набора на странице сбора: папка
// с моделью --- продолжение её обучения, и тогда модель, гиперпараметры,
// таблица и график берутся из истории модели и не правятся; любая другая
// папка --- место для новой модели, и форма возвращается к умолчаниям
// описания модели. Величины таблицы и графика объявляет описание модели;
// у каждой величины на графике своя ось: слева, справа или не показывать.
class TrainingPage : public AiPageBase
{
    Q_OBJECT
public:
    explicit TrainingPage(AppContext *context, QWidget *parent = nullptr);

private slots:
    void chooseDataset();
    void chooseOutput();
    void requestModelDescription();
    void train();
    void stop();
    void onMetric(int epoch, int epochs, const QJsonObject &values, bool best);
    void onFamilyMeta(const QJsonObject &meta);

private:
    // A value the model reports, its column, curve and axis choice.
    struct Output
    {
        QString name;
        QComboBox *axis = nullptr;
        QCPGraph *graph = nullptr;
    };

    void useOutput(const QString &path);
    // The form after the model folder changed: from the history of the
    // continued model, or the defaults of the model description.
    void applyOutputState();
    void loadModelFamilies();
    QStringList modelFamilies() const;
    void rebuildHyperparameters(const QJsonObject &meta);
    void fillHyperparameters(const nlohmann::json &values);
    void updateControls();
    void setupLossPlot();
    void rebuildOutputs(const std::vector<TrainingOutput> &outputs);
    void addEpoch(int epoch, const QJsonObject &values, bool best);
    void applyAxes();
    // Fits the axes to the visible curves while the operator has not zoomed.
    void rescalePlot();
    void applyLossPlotTheme();
    QJsonObject trainingConfig() const;

    QLineEdit *m_dataset;
    QLineEdit *m_output;
    QPushButton *m_datasetBrowse = nullptr;
    QPushButton *m_outputBrowse = nullptr;
    QComboBox *m_familyCombo = nullptr;
    QFormLayout *m_hyperparameterForm = nullptr;
    QHash<QString, QWidget *> m_hyperparameterWidgets;
    // Описание модели и модуль, для которого оно получено.
    QJsonObject m_familyMeta;
    QString m_metaFamily;
    QTableWidget *m_metrics;
    QVector<Output> m_outputs;
    QFormLayout *m_axesForm = nullptr;
    int m_bestRow = -1;
    QCustomPlot *m_lossPlot = nullptr;
    bool m_lossAutoScale = true;
    QPushButton *m_train;
    QPushButton *m_trainingStop = nullptr;
    TrainingSession *m_session = nullptr;
};

#endif // AI_TRAININGPAGE_H
