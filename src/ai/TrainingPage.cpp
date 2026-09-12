#include "ai/TrainingPage.h"
#include "ai/aimodel.h"
#include "ai/qtaiworkeradapter.h"
#include "navigation/AppContext.h"
#include "notifications/Notifications.h"
#include "ui_trainingpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

TrainingPage::TrainingPage(AppContext *c, QWidget *p) : AiPageBase(c, "Training", p)
{
    setObjectName(QStringLiteral("training"));
    if (c && c->notifications())
        c->notifications()->attachStatusLabel(objectName(), m_status);
    auto *l = qobject_cast<QVBoxLayout *>(layout());
    auto *content = new QWidget(this);
    Ui::TrainingPageForm form;
    form.setupUi(content);
    m_dataset = form.datasetPath;
    m_datasetBrowse = form.datasetBrowse;
    m_familyCombo = form.familyCombo;
    m_output = form.outputPath;
    m_outputBrowse = form.outputBrowse;
    m_hyperparameters = form.hyperparameters;
    m_hyperparameterForm = form.hyperparameterForm;
    m_progress = form.progress;
    m_metrics = form.metrics;
    m_train = form.startButton;
    m_trainingStop = form.stopButton;
    m_output->setText(QStringLiteral("models"));
    loadModelFamilies();
    m_metrics->horizontalHeader()->setStretchLastSection(true);
    m_metrics->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l->insertWidget(2, content, 1);
    connect(m_datasetBrowse, &QPushButton::clicked, this, &TrainingPage::chooseDataset);
    connect(m_outputBrowse, &QPushButton::clicked, this, &TrainingPage::chooseOutput);
    connect(m_familyCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
            {if(m_context) m_context->aiWorker()->describeFamily(m_familyCombo->currentText()); });
    connect(m_train, &QPushButton::clicked, this, &TrainingPage::train);
    connect(m_trainingStop, &QPushButton::clicked, this, &TrainingPage::stop);
    setTrainingControlsEnabled(false);
    if (c)
    {
        connect(c->aiWorker(), &QtAiWorkerAdapter::metric, this, &TrainingPage::onMetric);
        connect(c->aiWorker(), &QtAiWorkerAdapter::familyMeta, this, &TrainingPage::onFamilyMeta);
        connect(c->aiWorker(), &QtAiWorkerAdapter::modelReady, this, [this](const QString &p)
                {
                    if (!m_training) return;
                    const QString w = QFileInfo(QDir(p).filePath(QStringLiteral("weights.pt"))).exists()
                                          ? QStringLiteral("weights.pt") : QStringLiteral("weights.json");
                    writeModelManifest(p.toStdString(), m_spec, m_dataset->text().toStdString(), w.toStdString());
                    m_training = false;
                    setTrainingControlsEnabled(m_datasetValid);
                    m_trainingStop->setEnabled(false);
                    m_status->setText(tr("Model saved: %1").arg(p)); });
        connect(c->aiWorker(), &QtAiWorkerAdapter::errorOccurred, this, [this](const QString &e)
                { if (m_training) { m_training = false; setTrainingControlsEnabled(m_datasetValid); m_status->setText(tr("Error: %1").arg(e)); } });
        connect(c->aiWorker(), &QtAiWorkerAdapter::finished, this, [this](bool)
                { if (m_training) { m_training = false; setTrainingControlsEnabled(m_datasetValid); m_status->setText(tr("Training finished")); } });
    }
}

void TrainingPage::setTrainingControlsEnabled(bool enabled)
{
    if (m_datasetBrowse)
        m_datasetBrowse->setEnabled(!m_training);
    if (m_familyCombo)
        m_familyCombo->setEnabled(enabled);
    if (m_hyperparameters)
        m_hyperparameters->setEnabled(enabled);
    if (m_output)
        m_output->setEnabled(enabled);
    if (m_outputBrowse)
        m_outputBrowse->setEnabled(enabled);
    if (m_train)
        m_train->setEnabled(enabled && m_datasetValid && !m_training);
    if (m_trainingStop)
        m_trainingStop->setEnabled(m_training);
}

void TrainingPage::loadModelFamilies()
{
    if (!m_familyCombo)
        return;

    m_familyCombo->clear();
    const QDir modelsDir(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("ai_worker/models")));
    const QStringList files = modelsDir.entryList({QStringLiteral("*.py")},
                                                  QDir::Files, QDir::Name);
    for (const QString &file : files)
    {
        const QString family = QFileInfo(file).completeBaseName();
        if (family.isEmpty() || family == QStringLiteral("__init__") ||
            family.startsWith(QLatin1Char('_')) ||
            m_familyCombo->findText(family) >= 0)
            continue;
        m_familyCombo->addItem(family);
    }

    // Keep the built-in reference family available even if the runtime model
    // directory is temporarily unavailable (for example before deployment).
    if (m_familyCombo->findText(QStringLiteral("custom")) < 0)
        m_familyCombo->addItem(QStringLiteral("custom"));

    const int customIndex = m_familyCombo->findText(QStringLiteral("custom"));
    m_familyCombo->setCurrentIndex(customIndex >= 0 ? customIndex : 0);
}
void TrainingPage::chooseDataset()
{
    if (m_training)
        return;
    auto d = QFileDialog::getExistingDirectory(this, tr("Select dataset"), m_dataset->text());
    if (d.isEmpty())
        return;
    std::string err;
    auto dataset = Dataset::open(d.toStdString(), &err);
    if (!dataset)
    {
        m_context->notifications()->warning(this, tr("Dataset"), QString::fromStdString(err));
        return;
    }
    m_spec = dataset->spec();
    m_dataset->setText(d);
    m_datasetValid = true;
    setTrainingControlsEnabled(true);
    const QString family = m_familyCombo->currentText();
    if (m_context)
    {
        m_context->rememberDataset(d);
        m_context->aiWorker()->describeFamily(family);
    }
    m_status->setText(tr("Loaded: %1 (%2 channels, %3 samples)").arg(QString::fromStdString(m_spec.task.displayName)).arg(m_spec.channels.size()).arg(m_spec.windowSamples));
}
void TrainingPage::chooseOutput()
{
    auto d = QFileDialog::getExistingDirectory(this, tr("Select output folder"), m_output->text());
    if (!d.isEmpty())
        m_output->setText(d);
}
void TrainingPage::rebuildHyperparameters(const QJsonObject &meta)
{
    while (m_hyperparameterForm->count())
    {
        auto *item = m_hyperparameterForm->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_hyperparameterWidgets.clear();
    const QJsonArray params = meta.value(QStringLiteral("hyperparameters")).toArray();
    for (const QJsonValue &value : params)
    {
        const QJsonObject p = value.toObject();
        const QString name = p.value(QStringLiteral("name")).toString();
        const QString type = p.value(QStringLiteral("type")).toString();
        if (name.isEmpty())
            continue;
        QWidget *w = nullptr;
        if (type == QStringLiteral("int"))
        {
            auto *s = new QSpinBox(m_hyperparameters);
            s->setRange(p.value("min").toInt(std::numeric_limits<int>::min()), p.value("max").toInt(std::numeric_limits<int>::max()));
            s->setValue(p.value("default").toInt());
            w = s;
        }
        else if (type == QStringLiteral("float"))
        {
            auto *s = new QDoubleSpinBox(m_hyperparameters);
            s->setDecimals(8);
            s->setRange(p.value("min").toDouble(-1e9), p.value("max").toDouble(1e9));
            s->setValue(p.value("default").toDouble());
            w = s;
        }
        else if (type == QStringLiteral("enum"))
        {
            auto *s = new QComboBox(m_hyperparameters);
            for (const auto &c : p.value("choices").toArray())
                s->addItem(c.toString(), c.toVariant());
            const QString def = p.value("default").toString();
            if (!def.isEmpty())
                s->setCurrentText(def);
            w = s;
        }
        else if (type == QStringLiteral("bool"))
        {
            auto *s = new QCheckBox(m_hyperparameters);
            s->setChecked(p.value("default").toBool());
            w = s;
        }
        else
        {
            auto *s = new QLineEdit(m_hyperparameters);
            s->setText(p.value("default").toString());
            w = s;
        }
        if (w)
        {
            if (name.compare(QStringLiteral("seed"), Qt::CaseInsensitive) == 0)
                w->setToolTip(tr("Seed fixes random generators used by training, making splits and initialization reproducible."));
            m_hyperparameterWidgets.insert(name, w);
            m_hyperparameterForm->addRow(name + QStringLiteral(":"), w);
        }
    }
}
QJsonObject TrainingPage::trainingConfig() const
{
    QJsonObject config;
    for (auto it = m_hyperparameterWidgets.cbegin(); it != m_hyperparameterWidgets.cend(); ++it)
    {
        if (auto *s = qobject_cast<QSpinBox *>(it.value()))
            config[it.key()] = s->value();
        else if (auto *d = qobject_cast<QDoubleSpinBox *>(it.value()))
            config[it.key()] = d->value();
        else if (auto *c = qobject_cast<QComboBox *>(it.value()))
            config[it.key()] = c->currentData().isValid() ? QJsonValue::fromVariant(c->currentData()) : QJsonValue(c->currentText());
        else if (auto *b = qobject_cast<QCheckBox *>(it.value()))
            config[it.key()] = b->isChecked();
        else if (auto *e = qobject_cast<QLineEdit *>(it.value()))
            config[it.key()] = e->text();
    }
    return config;
}
void TrainingPage::onFamilyMeta(const QJsonObject &meta)
{
    if (!m_datasetValid)
        return;
    m_familyMeta = meta;
    rebuildHyperparameters(meta);
    m_status->setText(tr("Family ready: %1").arg(meta.value(QStringLiteral("name")).toString()));
}
void TrainingPage::train()
{
    if (!m_context || m_dataset->text().isEmpty() || !m_datasetValid)
    {
        m_status->setText(tr("Select a dataset first"));
        return;
    }
    if (m_spec.windowSamples == 0)
    {
        std::string err;
        auto dataset = Dataset::open(m_dataset->text().toStdString(), &err);
        if (!dataset)
        {
            m_status->setText(QString::fromStdString(err));
            return;
        }
        m_spec = dataset->spec();
    }
    const QString taskType = QString::fromStdString(toString(m_spec.task.taskType)), targetType = QString::fromStdString(toString(m_spec.task.targetType));
    if (!m_familyMeta.isEmpty() && (m_familyMeta.value(QStringLiteral("task_type")).toString() != taskType || m_familyMeta.value(QStringLiteral("target_type")).toString() != targetType))
    {
        m_status->setText(tr("Model family is incompatible with this dataset"));
        return;
    }
    // The selected family is a property of the model bundle, not of the
    // dataset. Store it before the worker reports modelReady so the manifest
    // points to the loader that produced the weights.
    m_spec.modelFamily = m_familyCombo->currentText().toStdString();
    auto out = QString::fromStdString(nextModelBundlePath(m_output->text().toStdString()));
    m_metrics->setRowCount(0);
    m_progress->setValue(0);
    m_training = true;
    setTrainingControlsEnabled(false);
    m_spec.extra["output_kind"] = m_familyMeta.value(QStringLiteral("output")).toObject().value(QStringLiteral("kind")).toString().toStdString();
    m_context->aiWorker()->startTrainingConfig(m_dataset->text(), out, m_familyCombo->currentText(), trainingConfig());
    m_status->setText(tr("Training started"));
}
void TrainingPage::stop()
{
    if (m_context)
        m_context->aiWorker()->stop();
    m_training = false;
    setTrainingControlsEnabled(m_datasetValid);
    m_status->setText(tr("Training stopped"));
}
void TrainingPage::onMetric(int epoch, int epochs, double tl, double vl, double acc)
{
    if (!m_training)
        return;
    m_progress->setValue(int(100.0 * epoch / std::max(1, epochs)));
    int r = m_metrics->rowCount();
    m_metrics->insertRow(r);
    for (int c = 0; c < 5; ++c)
        m_metrics->setItem(r, c, new QTableWidgetItem(c == 0 ? QString::number(epoch) : c == 1 ? QString::number(tl, 'f', 5)
                                                                                    : c == 2   ? QString::number(vl, 'f', 5)
                                                                                    : c == 3   ? QString::number(acc, 'f', 3)
                                                                                               : QString::number(100.0 * epoch / std::max(1, epochs), 'f', 1) + "%"));
}
