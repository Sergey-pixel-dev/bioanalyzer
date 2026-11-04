#include "navigation/TrainingPage.h"
#include "ai/trainingsession.h"
#include "ai/aimodel.h"
#include "navigation/AppContext.h"
#include "notifications/Notifications.h"
#include "settings/thememanager.h"
#include "ui_trainingpage.h"
#include "qcustomplot.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // Поле малого числа в экспоненциальной записи: 1e-3, 2.5e-4. Скорость
    // обучения и затухание весов подбирают в разы, а не в долях, поэтому
    // стрелки умножают и делят значение на 10. Запись --- с точкой при любом
    // языке интерфейса.
    class ExponentSpinBox : public QDoubleSpinBox
    {
    public:
        explicit ExponentSpinBox(QWidget *parent) : QDoubleSpinBox(parent)
        {
            // Без округления до знаков после запятой: 1e-7 не станет нулём.
            setDecimals(20);
        }

        QString textFromValue(double value) const override
        {
            if (value == 0.0)
                return QStringLiteral("0");
            const QString text = QString::number(value, 'e', 6);
            const int e = text.indexOf(QLatin1Char('e'));
            QString mantissa = text.left(e);
            while (mantissa.contains(QLatin1Char('.')) && mantissa.endsWith(QLatin1Char('0')))
                mantissa.chop(1);
            if (mantissa.endsWith(QLatin1Char('.')))
                mantissa.chop(1);
            return mantissa + QLatin1Char('e') + QString::number(text.mid(e + 1).toInt());
        }

        double valueFromText(const QString &text) const override
        {
            return QLocale::c().toDouble(text.trimmed());
        }

        QValidator::State validate(QString &text, int &) const override
        {
            static const QRegularExpression partial(QStringLiteral(R"(^\s*\d*\.?\d*([eE][-+]?\d*)?\s*$)"));
            if (!partial.match(text).hasMatch())
                return QValidator::Invalid;
            bool ok = false;
            const double value = QLocale::c().toDouble(text.trimmed(), &ok);
            if (!ok)
                return QValidator::Intermediate;
            return value >= minimum() && value <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
        }

        void stepBy(int steps) override
        {
            // От нуля умножать нечего: первый шаг вверх даёт наименьшее
            // значение поля или 1e-6, если наименьшее --- ноль.
            const double value = this->value();
            if (value <= 0.0)
                setValue(steps > 0 ? (minimum() > 0.0 ? minimum() : 1e-6) * std::pow(10.0, steps - 1) : 0.0);
            else
                setValue(value * std::pow(10.0, steps));
        }
    };

    std::vector<TrainingOutput> outputsFromMeta(const QJsonObject &meta)
    {
        std::vector<TrainingOutput> outputs;
        for (const auto &value : meta.value(QStringLiteral("outputs")).toObject().value(QStringLiteral("training")).toArray())
        {
            const QJsonObject item = value.toObject();
            const QString name = item.value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                outputs.push_back({name.toStdString(), item.value(QStringLiteral("axis")).toString().toStdString()});
        }
        return outputs;
    }

    // Цвета кривых по порядку величин; те же тона, что у графиков формы
    // просмотра.
    QColor curveColor(int index, bool dark)
    {
        static const QColor darkColors[] = {{90, 170, 240}, {240, 140, 90}, {120, 200, 120},
                                            {200, 130, 220}, {230, 200, 90}, {100, 210, 210}};
        static const QColor lightColors[] = {{0, 90, 160}, {190, 65, 15}, {30, 130, 40},
                                             {130, 60, 160}, {160, 120, 0}, {0, 130, 130}};
        return (dark ? darkColors : lightColors)[index % 6];
    }

    // При продолжении гиперпараметры --- из истории модели и не правятся.
    // Исключение --- устройство обучения: на результат оно не влияет, а без
    // него модель, начатую на видеокарте, нельзя было бы продолжить на
    // машине без видеокарты.
    bool changeableOnContinuation(const QString &name)
    {
        return name.compare(QStringLiteral("device"), Qt::CaseInsensitive) == 0;
    }
}

TrainingPage::TrainingPage(AppContext *c, QWidget *p) : AiPageBase(c, tr("Training"), p)
{
    setObjectName(QStringLiteral("training"));
    m_session = new TrainingSession(this);
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
    m_hyperparameterForm = form.hyperparameterForm;
    m_metrics = form.metrics;
    m_lossPlot = form.lossPlot;
    m_axesForm = form.axesForm;
    m_train = form.startButton;
    m_trainingStop = form.stopButton;
    // Гиперпараметры --- полоса сверху, ниже таблица и график; границы
    // областей оператор перетаскивает сам.
    form.splitter->setSizes({180, 520});
    form.splitter->setStretchFactor(1, 1);
    form.resultsSplitter->setSizes({400, 600});
    // По умолчанию новая модель создаётся в папке models.
    m_output->setText(QStringLiteral("models"));
    m_session->openOutput(m_output->text(), {});
    loadModelFamilies();
    m_metrics->horizontalHeader()->setStretchLastSection(true);
    m_metrics->verticalHeader()->setVisible(false);
    m_metrics->setEditTriggers(QAbstractItemView::NoEditTriggers);
    setupLossPlot();
    l->insertWidget(2, content, 1);
    connect(m_datasetBrowse, &QPushButton::clicked, this, &TrainingPage::chooseDataset);
    connect(m_outputBrowse, &QPushButton::clicked, this, &TrainingPage::chooseOutput);
    // Путь, введённый вручную и подтверждённый клавишей Enter, выбирается так
    // же, как кнопкой «Обзор». Уход фокуса папку не выбирает: выбор папки
    // сбрасывает форму, и щелчок по «Начать обучение» после ввода пути не
    // должен незаметно запустить обучение с умолчаниями.
    connect(m_output, &QLineEdit::returnPressed, this, [this] { useOutput(m_output->text().trimmed()); });
    connect(m_familyCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
            { requestModelDescription(); });
    connect(m_train, &QPushButton::clicked, this, &TrainingPage::train);
    connect(m_trainingStop, &QPushButton::clicked, this, &TrainingPage::stop);
    updateControls();
    // Страница общается только с сеансом обучения: процесс принадлежит ему, и
    // сеанс присылает готовые события --- описание модуля, эпоху, итог.
    connect(m_session, &TrainingSession::epochReported, this, &TrainingPage::onMetric);
    connect(m_session, &TrainingSession::described, this, &TrainingPage::onFamilyMeta);
    // Процесс занят описанием модели или обучением: кнопка запуска
    // недоступна, пока он не завершится.
    connect(m_session, &TrainingSession::workerChanged, this, [this] { updateControls(); });
    connect(m_session, &TrainingSession::describeFailed, this, [this](const QString &e)
            {
                updateControls();
                if (m_context && m_context->notifications())
                    m_context->notifications()->error(
                        this, tr("Model"),
                        tr("Cannot get the description of the model \"%1\": %2").arg(m_familyCombo->currentText(), e)); });
    connect(m_session, &TrainingSession::saved, this, [this](const QString &p)
            {
                updateControls();
                // Использованный seed --- в истории обучения модели: с ним
                // разбиение можно повторить.
                TrainingHistory history;
                if (loadTrainingHistory(p.toStdString(), history) && history.hyperparameters.contains("seed"))
                    setStatus(tr("Model saved: %1 (seed %2)")
                                  .arg(p, QString::fromStdString(history.hyperparameters.at("seed").dump())));
                else
                    setStatus(tr("Model saved: %1").arg(p)); });
    connect(m_session, &TrainingSession::finished, this, [this]
            {
                updateControls();
                setStatus(tr("Training finished")); });
    // Отказ рабочего процесса --- окно службы сообщений: обучение идёт долго,
    // и оператор может быть на другой странице.
    connect(m_session, &TrainingSession::failed, this, [this](const QString &reason)
            {
                updateControls();
                if (m_context && m_context->notifications())
                    m_context->notifications()->error(this, tr("Training"), reason); });
}

void TrainingPage::updateControls()
{
    const bool running = m_session->running();
    const bool dataset = m_session->datasetLoaded();
    const bool continuing = m_session->continuing();
    const bool described = !m_familyMeta.isEmpty() && m_metaFamily == m_familyCombo->currentText();
    m_datasetBrowse->setEnabled(!running);
    m_output->setEnabled(dataset && !running);
    m_outputBrowse->setEnabled(dataset && !running);
    // Продолжаемая модель задана папкой: модуль и гиперпараметры --- из её
    // истории и не правятся.
    m_familyCombo->setEnabled(dataset && !running && !continuing);
    for (auto it = m_hyperparameterWidgets.cbegin(); it != m_hyperparameterWidgets.cend(); ++it)
        it.value()->setEnabled(dataset && !running && (!continuing || changeableOnContinuation(it.key())));
    const bool epochsLeft = !continuing || m_session->epochsDone() < m_session->epochsPlanned();
    // Пока процесс сеанса занят (описание модели ещё не завершилось),
    // обучение не начать.
    m_train->setEnabled(dataset && described && !running && epochsLeft && !m_session->workerBusy());
    m_trainingStop->setEnabled(running);
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
    m_familyCombo->setCurrentIndex(0);
}

QStringList TrainingPage::modelFamilies() const
{
    QStringList families;
    for (int i = 0; i < m_familyCombo->count(); ++i)
        families << m_familyCombo->itemText(i);
    return families;
}

void TrainingPage::chooseDataset()
{
    if (m_session->running())
        return;
    auto d = QFileDialog::getExistingDirectory(this, tr("Select dataset"), m_dataset->text());
    if (d.isEmpty())
        return;
    QString error;
    if (!m_session->loadDataset(d, &error))
    {
        fail(error);
        return;
    }
    const auto &spec = m_session->spec();
    m_dataset->setText(d);
    // Продолжаемая модель должна подходить и к новому набору; иначе вместо
    // неё выбирается её родительская папка, где будет создана новая модель.
    if (!m_session->continuationValid(&error))
    {
        fail(error);
        useOutput(QFileInfo(m_session->outputDirectory()).absolutePath());
    }
    updateControls();
    requestModelDescription();
    setStatus(tr("Loaded: %1 (%2 channels, %3 samples)")
                  .arg(QString::fromStdString(spec.task.displayName))
                  .arg(spec.channels.size())
                  .arg(spec.windowSamples));
}

void TrainingPage::requestModelDescription()
{
    if (!m_familyCombo || m_familyCombo->currentText().isEmpty())
        return;
    m_metaFamily.clear();
    updateControls();
    m_session->describe(m_familyCombo->currentText());
}

void TrainingPage::chooseOutput()
{
    const auto d = QFileDialog::getExistingDirectory(this, tr("Select the model folder"), m_output->text());
    if (!d.isEmpty())
        useOutput(d);
}

void TrainingPage::useOutput(const QString &path)
{
    if (path.isEmpty() || m_session->running())
        return;
    // Та же папка выбирается заново, как любая: история модели перечитывается.
    QString error;
    if (!m_session->openOutput(path, modelFamilies(), &error))
    {
        // Папка не принята: остаётся прежний выбор.
        m_output->setText(m_session->outputDirectory());
        fail(error);
        return;
    }
    m_output->setText(path);
    applyOutputState();
}

void TrainingPage::applyOutputState()
{
    if (m_session->continuing())
    {
        const TrainingHistory &history = m_session->model().history();
        const QString family = QString::fromStdString(history.family);
        rebuildOutputs(history.outputs);
        for (const auto &epoch : history.epochs)
        {
            QJsonObject values;
            for (const auto &[name, value] : epoch.values)
                values.insert(QString::fromStdString(name), value);
            addEpoch(epoch.epoch, values, epoch.best);
        }
        m_lossAutoScale = true;
        rescalePlot();
        if (m_familyCombo->currentText() != family)
        {
            const QSignalBlocker blocker(m_familyCombo);
            m_familyCombo->setCurrentIndex(m_familyCombo->findText(family));
        }
        // Поля гиперпараметров строит описание модуля, значения --- из истории.
        if (m_metaFamily == family && !m_familyMeta.isEmpty())
        {
            rebuildHyperparameters(m_familyMeta);
            fillHyperparameters(history.hyperparameters);
        }
        else
            requestModelDescription();
        const int done = m_session->epochsDone();
        const int planned = m_session->epochsPlanned();
        setStatus(done < planned
                      ? tr("Continuing the model: %1 of %2 epochs passed").arg(done).arg(planned)
                      : tr("The model has passed all %1 epochs: there is nothing to continue").arg(planned));
    }
    else
    {
        // Новая папка --- новая модель: прежние значения формы сбрасываются
        // к умолчаниям описания модели.
        const bool described = !m_familyMeta.isEmpty() && m_metaFamily == m_familyCombo->currentText();
        rebuildOutputs(described ? outputsFromMeta(m_familyMeta) : std::vector<TrainingOutput>());
        if (described)
            rebuildHyperparameters(m_familyMeta);
        setStatus(tr("A new model will be created in %1").arg(m_session->outputDirectory()));
    }
    updateControls();
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
            auto *s = new QSpinBox(this);
            s->setRange(p.value("min").toInt(std::numeric_limits<int>::min()), p.value("max").toInt(std::numeric_limits<int>::max()));
            // Наименьшее значение необязательного параметра означает «не
            // задано»: так seed выбирает рабочий процесс и записывает его в
            // историю обучения.
            if (p.contains(QStringLiteral("special")))
                s->setSpecialValueText(tr("random"));
            s->setValue(p.value("default").toInt());
            w = s;
        }
        else if (type == QStringLiteral("float"))
        {
            // Малые величины (скорость обучения, затухание весов) описание
            // модели помечает форматом exp.
            QDoubleSpinBox *s = nullptr;
            if (p.value(QStringLiteral("format")).toString() == QStringLiteral("exp"))
                s = new ExponentSpinBox(this);
            else
            {
                s = new QDoubleSpinBox(this);
                s->setDecimals(8);
            }
            s->setRange(p.value("min").toDouble(-1e9), p.value("max").toDouble(1e9));
            s->setValue(p.value("default").toDouble());
            w = s;
        }
        else if (type == QStringLiteral("enum"))
        {
            auto *s = new QComboBox(this);
            for (const auto &c : p.value("choices").toArray())
                s->addItem(c.toString(), c.toVariant());
            const QString def = p.value("default").toString();
            if (!def.isEmpty())
                s->setCurrentText(def);
            w = s;
        }
        else if (type == QStringLiteral("bool"))
        {
            auto *s = new QCheckBox(this);
            s->setChecked(p.value("default").toBool());
            w = s;
        }
        else
        {
            auto *s = new QLineEdit(this);
            s->setText(p.value("default").toString());
            w = s;
        }
        if (name.compare(QStringLiteral("seed"), Qt::CaseInsensitive) == 0)
            w->setToolTip(tr("Seed of the random split into training and validation parts and of the initialization. "
                             "Leave it random for a new split; the seed used is saved in the training history of the model, "
                             "so that a run can be repeated on the same split."));
        m_hyperparameterWidgets.insert(name, w);
        m_hyperparameterForm->addRow(name + QStringLiteral(":"), w);
    }
}

void TrainingPage::fillHyperparameters(const nlohmann::json &values)
{
    for (auto it = m_hyperparameterWidgets.cbegin(); it != m_hyperparameterWidgets.cend(); ++it)
    {
        const std::string name = it.key().toStdString();
        if (!values.contains(name))
            continue;
        const auto &v = values.at(name);
        const QString text = QString::fromStdString(v.is_string() ? v.get<std::string>() : v.dump());
        if (auto *s = qobject_cast<QSpinBox *>(it.value()))
            s->setValue(v.is_number() ? v.get<int>() : s->value());
        else if (auto *d = qobject_cast<QDoubleSpinBox *>(it.value()))
            d->setValue(v.is_number() ? v.get<double>() : d->value());
        else if (auto *c = qobject_cast<QComboBox *>(it.value()))
            c->setCurrentIndex(std::max(0, c->findText(text)));
        else if (auto *b = qobject_cast<QCheckBox *>(it.value()))
            b->setChecked(v.is_boolean() ? v.get<bool>() : b->isChecked());
        else if (auto *e = qobject_cast<QLineEdit *>(it.value()))
            e->setText(text);
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
    m_familyMeta = meta;
    m_metaFamily = m_familyCombo->currentText();
    m_session->setFamilyMeta(meta);
    rebuildHyperparameters(meta);
    if (m_session->continuing())
        fillHyperparameters(m_session->model().history().hyperparameters);
    else
    {
        rebuildOutputs(outputsFromMeta(meta));
        setStatus(tr("Family ready: %1").arg(meta.value(QStringLiteral("name")).toString()));
    }
    updateControls();
}

void TrainingPage::train()
{
    // Путь, введённый без подтверждения, выбирается так же, как кнопкой
    // «Обзор», и форма заполняется заново; обучение не начинается, чтобы
    // оператор её проверил.
    const QString typed = m_output->text().trimmed();
    if (QDir::cleanPath(typed) != QDir::cleanPath(m_session->outputDirectory()))
    {
        useOutput(typed);
        if (QDir::cleanPath(typed) == QDir::cleanPath(m_session->outputDirectory()))
            setStatus(tr("The model folder is chosen: check the form and start again"));
        return;
    }
    QString error;
    if (!m_session->start(m_familyCombo->currentText(), trainingConfig(), &error))
    {
        fail(error);
        return;
    }
    // Новая модель начинается с пустых таблицы и графика; продолжаемая
    // дописывает свои эпохи к истории.
    if (!m_session->continuing())
        rebuildOutputs(outputsFromMeta(m_familyMeta));
    m_lossAutoScale = true;
    updateControls();
    setStatus(m_session->continuing() ? tr("Training continued") : tr("Training started"));
}

void TrainingPage::stop()
{
    m_session->stop();
    updateControls();
    setStatus(tr("Training stopped"));
}

void TrainingPage::onMetric(int epoch, int epochs, const QJsonObject &values, bool best)
{
    addEpoch(epoch, values, best);
    rescalePlot();
    setStatus(tr("Epoch %1 of %2").arg(epoch).arg(epochs));
}

void TrainingPage::rebuildOutputs(const std::vector<TrainingOutput> &outputs)
{
    while (m_axesForm->rowCount() > 0)
        m_axesForm->removeRow(0);
    m_lossPlot->clearGraphs();
    m_outputs.clear();
    m_bestRow = -1;
    QStringList headers{tr("Epoch")};
    for (const auto &output : outputs)
    {
        Output o;
        o.name = QString::fromStdString(output.name);
        o.graph = m_lossPlot->addGraph();
        o.graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 4));
        o.axis = new QComboBox(this);
        o.axis->addItem(tr("Left"), QStringLiteral("left"));
        o.axis->addItem(tr("Right"), QStringLiteral("right"));
        o.axis->addItem(tr("None", "axis"), QStringLiteral("none"));
        // Ось по умолчанию --- из описания модели; без неё величина скрыта.
        o.axis->setCurrentIndex(output.axis == "left" ? 0 : output.axis == "right" ? 1 : 2);
        connect(o.axis, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { applyAxes(); });
        m_axesForm->addRow(o.name + QStringLiteral(":"), o.axis);
        headers << o.name;
        m_outputs.push_back(o);
    }
    m_metrics->clear();
    m_metrics->setRowCount(0);
    m_metrics->setColumnCount(headers.size());
    m_metrics->setHorizontalHeaderLabels(headers);
    applyLossPlotTheme();
    applyAxes();
}

void TrainingPage::addEpoch(int epoch, const QJsonObject &values, bool best)
{
    auto setBold = [this](int row, bool bold)
    {
        for (int column = 0; row >= 0 && row < m_metrics->rowCount() && column < m_metrics->columnCount(); ++column)
            if (QTableWidgetItem *item = m_metrics->item(row, column))
            {
                QFont font = item->font();
                font.setBold(bold);
                item->setFont(font);
            }
    };
    // Продолжение повторяет эпохи после последней контрольной точки: строки и
    // точки с этой эпохи заменяются.
    while (m_metrics->rowCount() > 0 &&
           m_metrics->item(m_metrics->rowCount() - 1, 0)->data(Qt::UserRole).toInt() >= epoch)
        m_metrics->removeRow(m_metrics->rowCount() - 1);
    if (m_bestRow >= m_metrics->rowCount())
        m_bestRow = -1;
    for (const Output &o : m_outputs)
        o.graph->data()->removeAfter(epoch - 0.5);

    // Таблица следует за новыми эпохами, только пока её прокрутка стоит внизу:
    // оператор, прокрутивший её вверх, смотрит историю, и строка не должна
    // уводить его вниз. Докрутил до конца — следование возобновляется.
    QScrollBar *bar = m_metrics->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum();
    const int row = m_metrics->rowCount();
    m_metrics->insertRow(row);
    auto *number = new QTableWidgetItem(QString::number(epoch));
    number->setData(Qt::UserRole, epoch);
    m_metrics->setItem(row, 0, number);
    for (int i = 0; i < m_outputs.size(); ++i)
    {
        const Output &o = m_outputs[i];
        if (!values.contains(o.name))
            continue;
        const double value = values.value(o.name).toDouble();
        m_metrics->setItem(row, i + 1, new QTableWidgetItem(QString::number(value, 'g', 5)));
        o.graph->addData(epoch, value);
    }
    // Полужирная строка --- эпоха, веса которой сохранены для распознавания.
    if (best)
    {
        setBold(m_bestRow, false);
        m_bestRow = row;
        setBold(row, true);
    }
    if (follow)
        m_metrics->scrollToBottom();
}

void TrainingPage::applyAxes()
{
    bool right = false;
    for (const Output &o : m_outputs)
    {
        const QString axis = o.axis->currentData().toString();
        const bool shown = axis != QStringLiteral("none");
        o.graph->setValueAxis(axis == QStringLiteral("right") ? m_lossPlot->yAxis2 : m_lossPlot->yAxis);
        o.graph->setVisible(shown);
        o.graph->setName(axis == QStringLiteral("right") ? tr("%1 (right axis)").arg(o.name) : o.name);
        if (shown)
            o.graph->addToLegend();
        else
            o.graph->removeFromLegend();
        right = right || axis == QStringLiteral("right");
    }
    m_lossPlot->yAxis2->setVisible(right);
    m_lossPlot->legend->setVisible(m_lossPlot->legend->itemCount() > 0);
    rescalePlot();
}

void TrainingPage::rescalePlot()
{
    if (m_lossAutoScale)
    {
        m_lossPlot->xAxis->rescale(true);
        m_lossPlot->yAxis->rescale(true);
        m_lossPlot->yAxis2->rescale(true);
    }
    m_lossPlot->replot(QCustomPlot::rpQueuedReplot);
}

void TrainingPage::setupLossPlot()
{
    m_lossPlot->xAxis->setLabel(tr("Epoch"));
    // Эпохи --- целые: деления оси идут через целое число эпох.
    QSharedPointer<QCPAxisTickerFixed> epochs(new QCPAxisTickerFixed);
    epochs->setTickStep(1.0);
    epochs->setScaleStrategy(QCPAxisTickerFixed::ssMultiples);
    m_lossPlot->xAxis->setTicker(epochs);
    m_lossPlot->yAxis2->setTickLabels(true);
    // Колесо приближает и отдаляет, рамка, выделенная левой кнопкой,
    // увеличивает выделенную область; и то и другое снимает автомасштаб, а
    // средняя кнопка его возвращает. Обе оси значений меняются вместе.
    m_lossPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_lossPlot->setSelectionRectMode(QCP::srmZoom);
    m_lossPlot->axisRect()->setRangeDragAxes({m_lossPlot->xAxis}, {m_lossPlot->yAxis, m_lossPlot->yAxis2});
    m_lossPlot->axisRect()->setRangeZoomAxes({m_lossPlot->xAxis}, {m_lossPlot->yAxis, m_lossPlot->yAxis2});
    connect(m_lossPlot, &QCustomPlot::mousePress, this, [this](QMouseEvent *event)
            {
                if (event->button() == Qt::MiddleButton)
                {
                    m_lossAutoScale = true;
                    rescalePlot();
                }
                else
                    m_lossAutoScale = false; });
    connect(m_lossPlot, &QCustomPlot::mouseWheel, this, [this](QWheelEvent *)
            { m_lossAutoScale = false; });
    if (m_context && m_context->themeManager())
        connect(m_context->themeManager(), &ThemeManager::themeChanged, this,
                [this](AppTheme) { applyLossPlotTheme(); });
    applyLossPlotTheme();
}

void TrainingPage::applyLossPlotTheme()
{
    if (!m_lossPlot)
        return;
    // Те же цвета, что у графиков формы просмотра.
    const bool dark = !m_context || !m_context->themeManager() || m_context->themeManager()->isDark();
    const QColor background = dark ? QColor(24, 26, 31) : QColor(255, 255, 255);
    const QColor foreground = dark ? QColor(210, 214, 222) : QColor(45, 48, 55);
    const QColor grid = dark ? QColor(65, 70, 80) : QColor(205, 210, 218);
    m_lossPlot->setBackground(background);
    for (QCPAxis *axis : {m_lossPlot->xAxis, m_lossPlot->yAxis, m_lossPlot->yAxis2})
    {
        axis->setBasePen(QPen(foreground));
        axis->setTickPen(QPen(foreground));
        axis->setSubTickPen(QPen(foreground));
        axis->setTickLabelColor(foreground);
        axis->setLabelColor(foreground);
        axis->grid()->setPen(QPen(grid));
        axis->grid()->setSubGridPen(QPen(grid));
    }
    m_lossPlot->legend->setBrush(background);
    m_lossPlot->legend->setTextColor(foreground);
    m_lossPlot->legend->setBorderPen(QPen(grid));
    for (int i = 0; i < m_outputs.size(); ++i)
    {
        const QColor color = curveColor(i, dark);
        m_outputs[i].graph->setPen(QPen(color, 2));
        m_outputs[i].graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, color, 4));
    }
    m_lossPlot->replot(QCustomPlot::rpQueuedReplot);
}
