#ifndef AIPAGES_H
#define AIPAGES_H
#include <QWidget>
#include <QVector>
#include <QHash>
#include <QJsonObject>
#include <memory>
#include "adapters/qtdatasetcaptureadapter.h"
#include "adapters/qtdevicesessionadapter.h"
#include "core/dataset.h"
#include "core/datahub.h"
#include "core/slidingwindowaccumulator.h"
class AppContext;
class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QProgressBar;
class QTableWidget;
class QCustomPlot;
class QCheckBox;
class QFormLayout;
class QWidget;
class CueCaptureDialog;
class AiPageBase : public QWidget
{
public:
    explicit AiPageBase(AppContext *, const QString &, QWidget *parent = nullptr);

protected:
    AppContext *m_context;
    QLabel *m_status;
};
class DataCollectionPage : public AiPageBase
{
    Q_OBJECT
public:
    explicit DataCollectionPage(AppContext *, QWidget *parent = nullptr);
    ~DataCollectionPage() override;
private slots:
    void choosePath();
    void addLabel();
    void removeLabel();
    void chooseTask(int index);
    void start();
    void stop();
    void onCaptureProgress(int labelIndex, int repetition,
                           double seconds, const QString &label);
    void onCaptureCompleted(bool stopped);
    void onSessionChanged(QtDeviceSessionAdapter *session);

private:
    void refreshSpec();
    void loadTasks();
    QVector<quint8> selectedChannels() const;
    bool validateCapture(const DatasetSpec &spec, QString *error) const;
    DatasetSpec buildSpec(const QVector<quint8> &channels, int sampleRate) const;
    void updateTargetModeUi();
    void updateChannelWidgets();
    void updateCaptureRateUi();
    QComboBox *m_taskCombo = nullptr;
    QPushButton *m_start;
    QPushButton *m_stop;
    QPushButton *m_addLabelButton = nullptr;
    QPushButton *m_removeLabelButton = nullptr;
    QLineEdit *m_path;
    QLineEdit *m_taskId;
    QLineEdit *m_taskName;
    QSpinBox *m_prep;
    QSpinBox *m_duration;
    QLabel *m_sampleRateLabel = nullptr;
    QSpinBox *m_windowSamples = nullptr;
    QSpinBox *m_strideSamples = nullptr;
    QComboBox *m_units = nullptr;
    QDoubleSpinBox *m_scale = nullptr;
    QWidget *m_channelWidget = nullptr;
    QFormLayout *m_form = nullptr;
    QTableWidget *m_labels;
    QLabel *m_targetPlaceholder = nullptr;
    QVector<QCheckBox *> m_channels;
    TaskDescriptor m_descriptor;
    bool m_descriptorLoaded = false;
    std::unique_ptr<QtDatasetCaptureAdapter> m_capture;
    std::unique_ptr<CueCaptureDialog> m_cueDialog;
};
class TrainingPage : public AiPageBase
{
    Q_OBJECT
public:
    explicit TrainingPage(AppContext *, QWidget *parent = nullptr);
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
class InferencePage : public AiPageBase
{
    Q_OBJECT
public:
    explicit InferencePage(AppContext *, QWidget *parent = nullptr);
private slots:
    void chooseModel();
    void start();
    void stop();
    void onBlock(const QtDeviceSessionAdapter::SampleBlock &);
    void onStrideChanged(int samples);

private:
    QLineEdit *m_model;
    QLabel *m_descriptor;
    QTableWidget *m_top5;
    QSpinBox *m_inferenceStride = nullptr;
    QPushButton *m_modelBrowse = nullptr;
    QPushButton *m_start;
    QPushButton *m_stop;
    DatasetSpec m_spec;
    bool m_modelLoaded = false;
    SlidingWindowAccumulator m_windowAccumulator;
    uint64_t m_seen = 0;
    uint64_t m_windowsSent = 0;
    uint64_t m_resultsReceived = 0;
    bool m_running = false;
    bool m_acquisitionRequested = false;
};
#endif
