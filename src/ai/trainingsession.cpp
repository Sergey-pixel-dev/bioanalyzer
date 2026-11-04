#include "ai/trainingsession.h"
#include "ai/errortext.h"
#include <QFileInfo>
#include <QDir>

#include "ai/aimodel.h"
#include "ai/datasetindex.h"

#include <algorithm>

TrainingSession::TrainingSession(QObject *parent)
    : QObject(parent)
{
    // Процесс принадлежит сеансу, и его сигналы приходят только сюда: страница
    // получает от сеанса готовые события, а не разбирает, чьё это сообщение.
    connect(&m_worker, &QtAiWorkerAdapter::started, this, &TrainingSession::workerChanged);
    connect(&m_worker, &QtAiWorkerAdapter::familyMeta, this, [this](const QJsonObject &meta)
            {
                m_describing = false;
                m_familyMeta = meta;
                emit described(meta); });
    connect(&m_worker, &QtAiWorkerAdapter::metric, this,
            [this](int epoch, int epochs, const QJsonObject &values, bool best)
            {
                if (m_running)
                    emit epochReported(epoch, epochs, values, best); });
    connect(&m_worker, &QtAiWorkerAdapter::modelReady, this, [this](const QString &bundle)
            {
                if (!m_running)
                    return;
                m_running = false;
                reloadHistory();
                emit saved(bundle); });
    connect(&m_worker, &QtAiWorkerAdapter::errorOccurred, this, &TrainingSession::onWorkerError);
    connect(&m_worker, &QtAiWorkerAdapter::finished, this, &TrainingSession::onWorkerFinished);
}

void TrainingSession::describe(const QString &family)
{
    m_describing = true;
    m_worker.describeFamily(family);
}

void TrainingSession::onWorkerError(const QString &message)
{
    if (m_running)
    {
        m_running = false;
        discardUnfinished();
        reloadHistory();
        emit failed(tr("Training failed: %1").arg(message));
    }
    else if (m_describing)
    {
        m_describing = false;
        emit describeFailed(message);
    }
}

void TrainingSession::onWorkerFinished(bool ok)
{
    // Процесс завершился сам: обучение кончилось, если модель не была
    // сохранена раньше (modelReady), или отказало.
    if (m_running)
    {
        m_running = false;
        reloadHistory();
        if (ok)
            emit finished();
        else
        {
            discardUnfinished();
            emit failed(tr("The training process exited with an error"));
        }
    }
    m_describing = false;
    emit workerChanged();
}

bool TrainingSession::loadDataset(const QString &path, QString *error)
{
    if (m_running)
    {
        if (error)
            *error = tr("Stop training before changing the dataset");
        return false;
    }
    std::string err;
    auto dataset = Dataset::open(path.toStdString(), &err);
    if (!dataset)
    {
        if (error)
            *error = tr("Cannot open the dataset: %1").arg(errorText(err));
        return false;
    }
    m_spec = dataset->spec();
    m_datasetPath = path;
    m_datasetValid = true;
    return true;
}

bool TrainingSession::suitsDataset(const Model &model, QString *error) const
{
    auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (!m_datasetValid)
        return fail(tr("Select a dataset first"));
    // Окно модели --- из её гиперпараметров, как при создании модели; с
    // набором, нарезанным этим окном, модель сверяется по его содержимому.
    const auto &h = model.history().hyperparameters;
    const int windowMs = h.contains("window_ms") && h.at("window_ms").is_number() ? h.at("window_ms").get<int>() : 0;
    const int skipMs = windowMs > 0 && h.contains("skip_ms") && h.at("skip_ms").is_number() ? h.at("skip_ms").get<int>() : 0;
    DatasetSpec data;
    std::string err;
    if (!modelSpecFor(m_spec, windowMs, skipMs, data, &err) || !model.suits(data, &err))
        return fail(tr("The model was trained on other data: %1").arg(errorText(err)));
    return true;
}

bool TrainingSession::openOutput(const QString &path, const QStringList &families, QString *error)
{
    auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (m_running)
        return fail(tr("Stop training before changing the model folder"));
    if (path.trimmed().isEmpty())
        return fail(tr("Choose a model folder first"));
    if (!QFileInfo::exists(QDir(path).filePath(QStringLiteral("manifest.json"))))
    {
        m_outputDir = path;
        m_continuing = false;
        m_model = Model();
        return true;
    }
    // Папка с моделью: её обучение продолжается, если модель ещё есть среди
    // модулей, подходит к набору и оставила контрольную точку.
    Model model;
    std::string err;
    if (!model.open(path.toStdString(), &err))
        return fail(tr("Cannot open the model: %1").arg(errorText(err)));
    if (!families.contains(QString::fromStdString(model.history().family)))
        return fail(tr("The model was trained by the module \"%1\", which is not among the model modules")
                        .arg(QString::fromStdString(model.history().family)));
    if (!suitsDataset(model, error))
        return false;
    if (!QFileInfo::exists(QDir(path).filePath(QStringLiteral("last.pt"))))
        return fail(tr("The model has no checkpoint to continue from (last.pt)"));
    m_outputDir = path;
    m_model = std::move(model);
    m_continuing = true;
    return true;
}

bool TrainingSession::continuationValid(QString *error) const
{
    return !m_continuing || suitsDataset(m_model, error);
}

void TrainingSession::reloadHistory()
{
    if (m_continuing)
        m_model.reloadHistory();
}

int TrainingSession::epochsDone() const
{
    int done = 0;
    for (const auto &epoch : m_model.history().epochs)
        done = std::max(done, epoch.epoch);
    return done;
}

int TrainingSession::epochsPlanned() const
{
    return m_model.history().plannedEpochs;
}

bool TrainingSession::start(const QString &family, const QJsonObject &config, QString *error)
{
    auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };

    if (m_running)
        return fail(tr("Training is already running"));
    if (!m_datasetValid || m_datasetPath.isEmpty())
        return fail(tr("Select a dataset first"));
    if (m_outputDir.isEmpty())
        return fail(tr("Choose a model folder first"));

    if (m_spec.windowSamples == 0)
    {
        std::string err;
        auto dataset = Dataset::open(m_datasetPath.toStdString(), &err);
        if (!dataset)
            return fail(tr("Cannot open the dataset: %1").arg(errorText(err)));
        m_spec = dataset->spec();
    }

    // Классификатору нужно хотя бы два значения: с одним различать нечего,
    // а точность на контрольной части выглядела бы безупречной. Считаются
    // пробы, оставшиеся в выборке: исключённые на странице наборов обучение
    // пропускает.
    {
        DatasetIndex index;
        std::string err;
        if (!index.open(m_datasetPath.toStdString(), &err))
            return fail(tr("Cannot open the dataset: %1").arg(errorText(err)));
        if (index.includedLabels().size() < 2)
            return fail(index.excludedCount() > 0
                            ? tr("Fewer than two target values are left in the selection: "
                                 "return excluded trials on the Datasets page")
                            : tr("The dataset has fewer than two target values: there is nothing to tell apart"));
    }

    // Модель сверяется с тем, что в наборе: ей нужны целевые значения того
    // вида, на котором она учится. Задача, по которой набор собран, и её тип
    // роли не играют: что делать с метками, решает модель.
    const QString targetType = QString::fromStdString(toString(m_spec.task.targetType));
    const QString modelTarget = m_familyMeta.value(QStringLiteral("target_type")).toString();
    if (!m_familyMeta.isEmpty() && modelTarget != targetType)
        return fail(tr("The model learns from targets of the kind \"%1\", but the dataset has targets of the kind \"%2\"")
                        .arg(modelTarget, targetType));

    // Модуль модели --- свойство обученной модели, а не набора данных.
    // Записываем его до того, как процесс сообщит о готовности, чтобы
    // манифест указывал на загрузчик, которым получены веса. Вид ответа при
    // распознавании объявлен описанием модели (outputs.inference).
    m_spec.modelFamily = family.toStdString();
    m_spec.outputKind = m_familyMeta.value(QStringLiteral("outputs"))
                            .toObject()
                            .value(QStringLiteral("inference"))
                            .toObject()
                            .value(QStringLiteral("kind"))
                            .toString()
                            .toStdString();

    // Окно модели может быть короче пробы (гиперпараметр window_ms): в
    // манифест модели пишется именно оно, по нему распознавание накапливает
    // окна и с ним сравнивается продолжаемая модель.
    // Отрезки начинаются через skip_ms после команды, поэтому в пробу должно
    // поместиться окно вместе с отступом. Проверка идёт до создания модели:
    // отказ рабочего процесса пришёл бы, когда каталог модели уже создан.
    const int windowMs = config.value(QStringLiteral("window_ms")).toInt(0);
    const int skipMs = windowMs > 0 ? config.value(QStringLiteral("skip_ms")).toInt(0) : 0;
    DatasetSpec modelSpec;
    {
        std::string err;
        if (!modelSpecFor(m_spec, windowMs, skipMs, modelSpec, &err))
            return fail(tr("The model window with the skip (%1 ms) is longer than a trial of the dataset (%2 ms)")
                            .arg(windowMs + skipMs)
                            .arg(m_spec.sampleRate ? m_spec.windowSamples * 1000 / m_spec.sampleRate : 0));
    }
    // Тип задачи в манифесте модели --- то, что решает модель, по её описанию;
    // задача набора остаётся в нём сведением о происхождении данных.
    if (m_familyMeta.contains(QStringLiteral("task_type")))
        modelSpec.task.taskType =
            taskTypeFromString(m_familyMeta.value(QStringLiteral("task_type")).toString().toStdString());

    QJsonObject workerConfig = config;
    QString bundle;
    if (m_continuing)
    {
        // Продолжение: та же модель, по-прежнему подходящая к набору, и её
        // последняя контрольная точка. Гиперпараметры --- из истории, число
        // эпох тоже: продолжение лишь доводит прерванное обучение до конца.
        if (!m_familyMeta.value(QStringLiteral("resume")).toBool())
            return fail(tr("The model \"%1\" cannot continue training").arg(family));
        if (QString::fromStdString(m_model.history().family) != family)
            return fail(tr("The selected model is \"%1\", not \"%2\"")
                            .arg(QString::fromStdString(m_model.history().family), family));
        if (epochsDone() >= epochsPlanned())
            return fail(tr("The model has passed all %1 epochs: there is nothing to continue").arg(epochsPlanned()));
        QString reason;
        if (!continuationValid(&reason))
            return fail(reason);
        bundle = m_outputDir;
        const QString checkpoint = QDir(bundle).filePath(QStringLiteral("last.pt"));
        if (!QFileInfo::exists(checkpoint))
            return fail(tr("The model has no checkpoint to continue from (last.pt)"));
        workerConfig[QStringLiteral("resume")] = checkpoint;
    }
    else
    {
        bundle = QString::fromStdString(nextModelBundlePath(m_outputDir.toStdString()));
    }
    // Манифест пишется сразу: контрольные точки сохраняются после каждой
    // эпохи, и прерванное обучение оставляет модель, пригодную к
    // распознаванию (с лучшими на момент остановки весами).
    std::string manifestError;
    QDir().mkpath(bundle);
    const std::string weights =
        m_familyMeta.value(QStringLiteral("weights")).toString(QStringLiteral("weights.pt")).toStdString();
    if (!writeModelManifest(bundle.toStdString(), modelSpec, m_datasetPath.toStdString(), weights, &manifestError))
        return fail(tr("Cannot write the model: %1").arg(errorText(manifestError)));
    m_bundle = bundle;
    m_resumed = m_continuing;
    m_running = true;
    m_worker.startTrainingConfig(m_datasetPath, bundle, family, workerConfig);
    return true;
}

void TrainingSession::stop()
{
    m_worker.stop();
    if (!m_running)
        return;
    m_running = false;
    // Остановленное обучение оставляет пройденные эпохи в истории модели.
    reloadHistory();
    emit stopped();
}

void TrainingSession::discardUnfinished()
{
    // Обучение, не дошедшее до первой контрольной точки, оставило бы каталог
    // модели без весов, которую нельзя ни распознавать, ни продолжить.
    // Удаляется только созданное этим сеансом и только без весов.
    if (m_resumed || m_bundle.isEmpty())
        return;
    QDir dir(m_bundle);
    const QString weights = m_familyMeta.value(QStringLiteral("weights")).toString(QStringLiteral("weights.pt"));
    if (dir.exists(weights))
        return;
    for (const QString &file : {QStringLiteral("manifest.json"), QStringLiteral("history.json"),
                                QStringLiteral("training.json")})
        dir.remove(file);
    QDir().rmdir(m_bundle);
}
