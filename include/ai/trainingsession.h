#ifndef AI_TRAININGSESSION_H
#define AI_TRAININGSESSION_H

#include "ai/aimodel.h"
#include "ai/dataset.h"
#include "ai/qtaiworkeradapter.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

// Управляющий класс варианта использования «Обучить модель распознавания».
// Хранит выбранный набор данных, описание модуля модели, папку результата и
// состояние обучения; проверяет совместимость модели с набором и ведёт
// обучение в своём рабочем процессе: запрашивает у него описание модуля,
// запускает и останавливает обучение и принимает величины по эпохам. Страница
// обучения с процессом не общается --- только с сеансом. Регистрацию не
// захватывает.
//
// Папка результата выбирается так же, как папка набора при сборе: папка с
// моделью --- продолжение её обучения (модель, гиперпараметры и пройденные
// эпохи --- из её истории), любая другая --- место для новой модели.
class TrainingSession : public QObject
{
    Q_OBJECT
public:
    explicit TrainingSession(QObject *parent = nullptr);

    bool loadDataset(const QString &path, QString *error = nullptr);
    // Asks the worker for the description of the model module `family`; the
    // answer comes as described() or describeFailed().
    void describe(const QString &family);
    void setFamilyMeta(const QJsonObject &meta) { m_familyMeta = meta; }

    // A folder with a model continues it: the model must be one of `families`,
    // suit the dataset and have a checkpoint to continue from; otherwise the
    // folder is refused and the previous choice stays. Any other folder
    // receives a new model.
    bool openOutput(const QString &path, const QStringList &families, QString *error = nullptr);
    // The continued model suits the dataset loaded now.
    bool continuationValid(QString *error = nullptr) const;
    bool continuing() const { return m_continuing; }
    // The continued model: its description and its history.
    const Model &model() const { return m_model; }
    int epochsDone() const;
    int epochsPlanned() const;

    // A new model is created in the output folder; a continued one goes on
    // from its last checkpoint and overwrites its best and last checkpoints.
    bool start(const QString &family, const QJsonObject &config, QString *error = nullptr);
    void stop();

    bool running() const { return m_running; }
    // The worker is busy: it describes a module or trains.
    bool workerBusy() const { return m_worker.isRunning(); }
    bool datasetLoaded() const { return m_datasetValid; }
    const DatasetSpec &spec() const { return m_spec; }
    const QString &datasetPath() const { return m_datasetPath; }
    const QString &outputDirectory() const { return m_outputDir; }

signals:
    void described(const QJsonObject &meta);
    void describeFailed(const QString &reason);
    // Values the model reports after an epoch, named as in its description;
    // `best` --- the weights of this epoch are kept for recognition.
    void epochReported(int epoch, int epochs, const QJsonObject &values, bool best);
    // The worker saved the model and its training is over.
    void saved(const QString &bundlePath);
    // The worker ended the training without reporting a saved model.
    void finished();
    void stopped();
    void failed(const QString &reason);
    // The worker started or ended: training can or cannot start now.
    void workerChanged();

private:
    // The model `model` suits the loaded dataset.
    bool suitsDataset(const Model &model, QString *error) const;
    // Re-reads the history of the continued model after a run.
    void reloadHistory();
    // После отказа: удаляет созданную этим запуском модель, если весов у неё
    // ещё нет.
    void discardUnfinished();
    void onWorkerError(const QString &message);
    void onWorkerFinished(bool ok);

    QtAiWorkerAdapter m_worker;
    DatasetSpec m_spec;
    QJsonObject m_familyMeta;
    QString m_datasetPath;
    QString m_outputDir;
    QString m_bundle;
    Model m_model;
    bool m_continuing = false;
    bool m_resumed = false;
    bool m_datasetValid = false;
    bool m_running = false;
    bool m_describing = false;
};

#endif // AI_TRAININGSESSION_H
