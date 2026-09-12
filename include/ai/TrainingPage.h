#ifndef AI_TRAININGPAGE_H
#define AI_TRAININGPAGE_H

#include "ai/AiPageBase.h"
#include "ai/dataset.h"

#include <QHash>
#include <QJsonObject>

class QLineEdit;
class QComboBox;
class QPushButton;
class QFormLayout;
class QProgressBar;
class QTableWidget;

class TrainingPage : public AiPageBase
{
    Q_OBJECT
public:
    explicit TrainingPage(AppContext *context, QWidget *parent = nullptr);

private slots:
    void chooseDataset();
    void chooseOutput();
    void train();
    void stop();
    void onMetric(int, int, double, double, double);
    void onFamilyMeta(const QJsonObject &meta);

private:
    void loadModelFamilies();
    void rebuildHyperparameters(const QJsonObject &meta);
    void setTrainingControlsEnabled(bool enabled);
    QJsonObject trainingConfig() const;

    QLineEdit *m_dataset;
    QLineEdit *m_output;
    QPushButton *m_datasetBrowse = nullptr;
    QPushButton *m_outputBrowse = nullptr;
    QComboBox *m_familyCombo = nullptr;
    QWidget *m_hyperparameters = nullptr;
    QFormLayout *m_hyperparameterForm = nullptr;
    QHash<QString, QWidget *> m_hyperparameterWidgets;
    QJsonObject m_familyMeta;
    QProgressBar *m_progress;
    QTableWidget *m_metrics;
    QPushButton *m_train;
    QPushButton *m_trainingStop = nullptr;
    DatasetSpec m_spec;
    bool m_datasetValid = false;
    bool m_training = false;
};

#endif // AI_TRAININGPAGE_H
