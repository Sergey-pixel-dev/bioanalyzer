#ifndef AI_DATACOLLECTIONPAGE_H
#define AI_DATACOLLECTIONPAGE_H

#include "navigation/AiPageBase.h"

#include <QStringList>
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
    void onCueChanged(int cycle, const QString &label, bool recording, int seconds);
    void onCaptureProgress(int cycle, quint64 examples);
    void onCaptureCompleted(bool stopped);
    // Продолжение набора: задача, длительности, жесты и повторения --- из него.
    void fillFromDataset(const QString &path);
    void onSessionChanged(QtDeviceSessionAdapter *session);

private:
    void refreshSpec();
    void loadTasks();
    bool validateCapture(const DatasetSpec &spec, QString *error) const;
    DatasetSpec buildSpec(const QVector<quint8> &channels, int sampleRate) const;
    // Доступность полей и кнопок: до выбора папки форма недоступна, при
    // продолжении набора правятся только повторения, «Начать сбор» --- только
    // при идущем потоке.
    void updateFormState();
    void updateCaptureRateUi();
    // Продолжение набора: задача, длительности и жесты заданы набором и не
    // правятся, меняется только число повторений (0 --- жест в этом сеансе
    // не записывается).
    void setContinuation(bool on);
    // Папка с исправным набором --- продолжение этого набора, любая другая ---
    // место для нового набора, и форма возвращается к описанию задачи.
    // Повторный выбор той же папки перечитывает её.
    void useFolder(const QString &path);
    // Окно = частота дискретизации × длительность записи пробы.
    void updateWindowSamples();

    QComboBox *m_taskCombo = nullptr;
    QPushButton *m_start;
    QPushButton *m_stop;
    QPushButton *m_addLabelButton = nullptr;
    QPushButton *m_removeLabelButton = nullptr;
    QLineEdit *m_path;
    QSpinBox *m_prep;
    QSpinBox *m_duration;
    QLabel *m_sampleRateLabel = nullptr;
    QSpinBox *m_windowSamples = nullptr;
    QFormLayout *m_form = nullptr;
    QTableWidget *m_labels;
    QLabel *m_targetPlaceholder = nullptr;
    TaskDescriptor m_descriptor;
    bool m_descriptorLoaded = false;
    // Выбранная папка; пока она пуста, форма недоступна.
    QString m_folder;
    // Форма заполнена из продолжаемого набора и заблокирована.
    bool m_continuing = false;
    // Усиление каналов продолжаемого набора: поток должен идти с ним же.
    std::vector<int> m_continuedGain;
    std::unique_ptr<QtDatasetCaptureAdapter> m_capture;
    std::unique_ptr<CueCaptureDialog> m_cueDialog;
    quint64 m_examplesWritten = 0;
    // Подозрительные пробы этого сбора: номер в наборе и причины.
    QStringList m_suspiciousTrials;
    QStringList m_repeatedTrials;
    // Последняя подозрительная проба: её можно повторить.
    struct SuspiciousTrial
    {
        int cycle = -1;
        quint64 id = 0;
        QString label;
    } m_lastSuspicious;
};

#endif // AI_DATACOLLECTIONPAGE_H
