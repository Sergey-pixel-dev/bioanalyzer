#ifndef AI_DATACOLLECTIONPAGE_H
#define AI_DATACOLLECTIONPAGE_H

#include "ai/AiPageBase.h"
#include "ai/dataset.h"
#include "ai/qtdatasetcaptureadapter.h"
#include "device/qtdevicesessionadapter.h"

#include <QVector>
#include <memory>

class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QCheckBox;
class QFormLayout;
class CueCaptureDialog;

class DataCollectionPage : public AiPageBase
{
    Q_OBJECT
public:
    explicit DataCollectionPage(AppContext *context, QWidget *parent = nullptr);
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

#endif // AI_DATACOLLECTIONPAGE_H
