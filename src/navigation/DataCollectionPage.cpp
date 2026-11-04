#include "navigation/DataCollectionPage.h"
#include "navigation/qualitytext.h"
#include "navigation/CueCaptureDialog.h"
#include "ai/qtdatasetcaptureadapter.h"
#include "device/qtdevicesessionadapter.h"
#include "navigation/AppContext.h"
#include "notifications/Notifications.h"
#include "ui_datacollectionpage.h"
#include <QSignalBlocker>
#include <QTimer>
#include <QFileInfo>
#include "ai/errortext.h"
#include <functional>
#include <QSet>
#include <QStyledItemDelegate>

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <random>

namespace
{
    // Исправен ли набор, проверяет его открытие; здесь --- лишь то, что папка
    // и есть набор, а не место для нового.
    bool isDatasetFolder(const QString &path)
    {
        return !path.isEmpty() && QFileInfo::exists(QDir(path).filePath(QStringLiteral("manifest.json")));
    }

    // Редакторы таблицы жестов. Число повторений вводится полем со счётчиком
    // (целое от 1 до 999, при продолжении набора от 0), поэтому буквы в него
    // не попадают. Метка должна
    // быть непустой и не повторять другую строку без учёта регистра:
    // недопустимое значение не принимается, прежнее остаётся, а причина
    // выводится в строке состояния страницы.
    class LabelTableDelegate : public QStyledItemDelegate
    {
    public:
        LabelTableDelegate(std::function<void(const QString &)> reject, std::function<int()> minimum,
                           QObject *parent)
            : QStyledItemDelegate(parent), m_reject(std::move(reject)), m_minimum(std::move(minimum)) {}

        QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override
        {
            if (index.column() == 1)
            {
                auto *spin = new QSpinBox(parent);
                spin->setRange(m_minimum(), 999);
                spin->setFrame(false);
                return spin;
            }
            auto *edit = qobject_cast<QLineEdit *>(QStyledItemDelegate::createEditor(parent, option, index));
            if (edit)
                edit->setMaxLength(64);
            return edit;
        }

        void setEditorData(QWidget *editor, const QModelIndex &index) const override
        {
            if (auto *spin = qobject_cast<QSpinBox *>(editor))
            {
                spin->setValue(std::max(m_minimum(), index.data().toString().toInt()));
                return;
            }
            QStyledItemDelegate::setEditorData(editor, index);
        }

        void setModelData(QWidget *editor, QAbstractItemModel *model,
                          const QModelIndex &index) const override
        {
            if (auto *spin = qobject_cast<QSpinBox *>(editor))
            {
                spin->interpretText();
                model->setData(index, QString::number(spin->value()));
                return;
            }
            auto *edit = qobject_cast<QLineEdit *>(editor);
            if (!edit)
                return;
            const QString label = edit->text().trimmed();
            if (label.isEmpty())
            {
                m_reject(QCoreApplication::translate("DataCollectionPage", "A gesture label cannot be empty"));
                return;
            }
            for (int row = 0; row < model->rowCount(); ++row)
                if (row != index.row() &&
                    model->index(row, 0).data().toString().trimmed().compare(label, Qt::CaseInsensitive) == 0)
                {
                    m_reject(QCoreApplication::translate("DataCollectionPage", "The label \"%1\" is already in the table").arg(label));
                    return;
                }
            model->setData(index, label);
        }

    private:
        std::function<void(const QString &)> m_reject;
        std::function<int()> m_minimum;
    };
}

DataCollectionPage::DataCollectionPage(AppContext *c, QWidget *p)
    : AiPageBase(c, tr("Data Collection"), p)
{
    setObjectName(QStringLiteral("data_collection"));
    if (c && c->notifications())
        c->notifications()->attachStatusLabel(objectName(), m_status);
    auto *root = qobject_cast<QVBoxLayout *>(layout());
    auto *content = new QWidget(this);
    Ui::DataCollectionPageForm form;
    form.setupUi(content);
    root->insertWidget(2, content, 1);

    m_path = form.datasetPath;
    // Путь, введённый вручную и подтверждённый клавишей Enter, выбирается так
    // же, как кнопкой «Обзор». Уход фокуса папку не выбирает: выбор папки
    // сбрасывает форму, и щелчок по «Начать сбор» после ввода пути не должен
    // незаметно начать сбор по умолчаниям задачи.
    connect(m_path, &QLineEdit::returnPressed, this, [this] { useFolder(m_path->text().trimmed()); });
    m_taskCombo = form.taskCombo;
    m_prep = form.preparationSeconds;
    m_duration = form.recordingSeconds;
    m_sampleRateLabel = form.sampleRateLabel;
    m_windowSamples = form.windowSamples;
    m_labels = form.labelsTable;
    m_targetPlaceholder = form.targetPlaceholder;
    m_start = form.startButton;
    m_stop = form.stopButton;
    m_addLabelButton = form.addLabelButton;
    m_removeLabelButton = form.removeLabelButton;
    m_stop->setEnabled(false);
    m_form = form.settingsForm;
    m_labels->setItemDelegate(new LabelTableDelegate(
        [this](const QString &reason)
        { fail(reason); },
        [this] { return m_continuing ? 0 : 1; }, m_labels));

    connect(form.browseButton, &QPushButton::clicked, this, &DataCollectionPage::choosePath);
    /*TODO, индекс при запуске получается меняется? чтобы вызывался chooseTask, это необходимое условие */
    connect(m_taskCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &DataCollectionPage::chooseTask);
    connect(form.addLabelButton, &QPushButton::clicked, this, &DataCollectionPage::addLabel);
    connect(form.removeLabelButton, &QPushButton::clicked, this, &DataCollectionPage::removeLabel);
    connect(m_start, &QPushButton::clicked, this, &DataCollectionPage::start);
    // Одна проба — один пример: окно равно всей записи пробы и следует за её
    // длительностью.
    connect(m_duration, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { updateWindowSamples(); });
    connect(m_stop, &QPushButton::clicked, this, &DataCollectionPage::stop);
    loadTasks();
    if (m_taskCombo->count() == 0)
        addLabel();
    if (c)
        m_capture = std::make_unique<QtDatasetCaptureAdapter>(c->dataHub(), this);
    if (c)
    {
        connect(c, &AppContext::sessionChanged,
                this, &DataCollectionPage::onSessionChanged);
        onSessionChanged(c->session());
    }
}

DataCollectionPage::~DataCollectionPage() = default;
void DataCollectionPage::loadTasks()
{
    m_taskCombo->clear();
    const QDir dir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tasks")));
    QStringList broken;
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        TaskDescriptor d;
        std::string err;
        if (!loadTaskDescriptor(dir.filePath(file).toStdString(), d, &err))
        {
            // Испорченное описание не пропускается молча: иначе задача просто
            // исчезает из списка, и причину не найти.
            broken << tr("%1: %2").arg(file, errorText(err));
            continue;
        }
        // Описание задачи, которую эта версия не реализует, отвергает уже
        // loadTaskDescriptor() --- с причиной в том же окне.
        m_taskCombo->addItem(QString::fromStdString(d.displayName), dir.filePath(file));
    }
    if (m_taskCombo->count() > 0)
        chooseTask(0);
    if (!broken.isEmpty())
        // После показа главного окна, а не при его создании.
        QTimer::singleShot(0, this, [this, broken]
                           { fail(tr("Some task descriptors could not be read and are skipped:\n%1")
                                      .arg(broken.join(QLatin1Char('\n')))); });
}
void DataCollectionPage::chooseTask(int index)
{
    if (index < 0 || index >= m_taskCombo->count())
        return;
    TaskDescriptor d;
    std::string err;
    if (!loadTaskDescriptor(m_taskCombo->itemData(index).toString().toStdString(), d, &err))
    {
        fail(tr("Task error: %1").arg(errorText(err)));
        m_descriptorLoaded = false;
        return;
    }

    m_descriptor = d;
    m_descriptorLoaded = true;
    // Идентификатор задачи виден в подсказке; отдельных полей для него нет.
    m_taskCombo->setToolTip(QString::fromStdString(d.taskId));
    m_prep->setValue(d.paradigm.prepSeconds);
    m_duration->setValue(d.paradigm.recordSeconds);
    updateWindowSamples();
    m_labels->setRowCount(0);
    for (const auto &e : d.events)
    {
        if (e.target.type != TargetType::Label)
            continue;
        const int r = m_labels->rowCount();
        m_labels->insertRow(r);
        m_labels->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(e.target.label)));
        m_labels->setItem(r, 1, new QTableWidgetItem(QStringLiteral("1")));
    }
    updateFormState();
    if (m_labels->rowCount() == 0 && d.targetType == TargetType::Label)
        addLabel();
}
void DataCollectionPage::choosePath()
{
    auto d = QFileDialog::getExistingDirectory(this, tr("Select dataset folder"), m_path->text());
    if (d.isEmpty())
        return;
    m_path->setText(d);
    useFolder(d);
}

void DataCollectionPage::useFolder(const QString &path)
{
    if (path.isEmpty() || (m_capture && m_capture->running()))
        return;
    // Та же папка выбирается заново, как любая: набор перечитывается, и форма
    // показывает его нынешнее описание.
    if (isDatasetFolder(path))
    {
        fillFromDataset(path);
        return;
    }
    // Новая папка --- новый набор: значения, оставшиеся от прежней папки, в
    // том числе от продолженного набора, сбрасываются к описанию задачи.
    m_folder = path;
    setContinuation(false);
    chooseTask(m_taskCombo->currentIndex());
    setStatus(tr("A new dataset will be created in %1").arg(path));
}

void DataCollectionPage::fillFromDataset(const QString &path)
{
    std::string err;
    const auto dataset = Dataset::open(path.toStdString(), &err);
    if (!dataset)
    {
        // Набор, который не открывается, не продолжается, и новый набор
        // внутри него не создаётся: папка не выбрана.
        m_folder.clear();
        setContinuation(false);
        fail(tr("Cannot open the dataset: %1").arg(errorText(err)));
        return;
    }
    const DatasetSpec &spec = dataset->spec();
    // Продолжить можно только ту задачу, для которой набор собран.
    int taskIndex = -1;
    for (int i = 0; i < m_taskCombo->count() && taskIndex < 0; ++i)
    {
        TaskDescriptor descriptor;
        if (loadTaskDescriptor(m_taskCombo->itemData(i).toString().toStdString(), descriptor) &&
            descriptor.taskId == spec.task.taskId)
            taskIndex = i;
    }
    if (taskIndex < 0)
    {
        m_folder.clear();
        setContinuation(false);
        fail(tr("The dataset was collected for the task \"%1\", which is not among the task descriptions")
                 .arg(QString::fromStdString(spec.task.taskId)));
        return;
    }
    {
        const QSignalBlocker blocker(m_taskCombo);
        m_taskCombo->setCurrentIndex(taskIndex);
    }
    chooseTask(taskIndex);
    // Длительности и жесты --- из набора: новый сеанс по умолчанию повторяет
    // план последнего сеанса, включая пропущенные в нём жесты (0).
    m_prep->setValue(spec.task.paradigm.prepSeconds);
    m_duration->setValue(spec.task.paradigm.recordSeconds);
    m_labels->setRowCount(0);
    for (const auto &event : spec.task.events)
    {
        if (event.target.type != TargetType::Label)
            continue;
        const int row = m_labels->rowCount();
        m_labels->insertRow(row);
        m_labels->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(event.target.label)));
        m_labels->setItem(row, 1, new QTableWidgetItem(QString::number(std::max(0, event.repetitions))));
    }
    updateWindowSamples();
    m_folder = path;
    m_continuedGain = spec.gain;
    setContinuation(true);
    setStatus(tr("Continuing the dataset: the task, durations and gestures are its own; only the repetitions "
                 "can be changed, 0 skips a gesture in this session"));
}

void DataCollectionPage::setContinuation(bool on)
{
    m_continuing = on;
    if (!on)
        m_continuedGain.clear();
    for (int row = 0; row < m_labels->rowCount(); ++row)
    {
        if (QTableWidgetItem *label = m_labels->item(row, 0))
            label->setFlags(on ? label->flags() & ~Qt::ItemIsEditable : label->flags() | Qt::ItemIsEditable);
        // Новый набор пропускать жесты не умеет: такую строку проще удалить.
        if (QTableWidgetItem *count = m_labels->item(row, 1); !on && count && count->text().toInt() < 1)
            count->setText(QStringLiteral("1"));
    }
    updateCaptureRateUi();
}
void DataCollectionPage::addLabel()
{
    // Новая метка не должна совпасть с уже имеющейся, даже если строки
    // удалялись: берётся наименьший свободный номер.
    QSet<QString> used;
    for (int row = 0; row < m_labels->rowCount(); ++row)
        if (const auto *item = m_labels->item(row, 0))
            used.insert(item->text().trimmed().toLower());
    int number = 1;
    while (used.contains(QStringLiteral("gesture_%1").arg(number)))
        ++number;
    const int r = m_labels->rowCount();
    m_labels->insertRow(r);
    m_labels->setItem(r, 0, new QTableWidgetItem(QStringLiteral("gesture_%1").arg(number)));
    m_labels->setItem(r, 1, new QTableWidgetItem(QStringLiteral("1")));
}
void DataCollectionPage::removeLabel()
{
    if (m_labels->currentRow() >= 0)
        m_labels->removeRow(m_labels->currentRow());
}
void DataCollectionPage::refreshSpec() { chooseTask(m_taskCombo ? m_taskCombo->currentIndex() : -1); }

void DataCollectionPage::onSessionChanged(QtDeviceSessionAdapter *session)
{
    if (session)
    {
        connect(session, &QtDeviceSessionAdapter::connectionChanged,
                this, [this](bool) { updateCaptureRateUi(); });
        // Без потока пробы не наполняются: сбор завершается, и набор
        // сохраняется с уже записанными пробами. Начать сбор можно только при
        // идущем потоке.
        connect(session, &QtDeviceSessionAdapter::streamingChanged, this, [this](bool streaming)
                {
                    if (!streaming && m_capture && m_capture->running())
                    {
                        m_capture->stop();
                        fail(tr("The stream stopped: capture ended, completed trials are saved"));
                    }
                    updateFormState(); });
        connect(session, &QtDeviceSessionAdapter::commandAck,
                this, [this](int, bool, int)
                { updateCaptureRateUi(); });
        updateCaptureRateUi();
    }
    else
    {
        m_sampleRateLabel->setText(tr("Not connected"));
        updateFormState();
    }
}

void DataCollectionPage::updateCaptureRateUi()
{
    const int rate = m_context && m_context->session() ? m_context->session()->sampleRateHz() : 0;
    m_sampleRateLabel->setText(rate > 0 ? tr("%1 Hz").arg(rate) : tr("Not connected"));
    updateFormState();
    updateWindowSamples();
}

void DataCollectionPage::updateWindowSamples()
{
    const int rate = m_context && m_context->session() ? m_context->session()->sampleRateHz() : 0;
    m_windowSamples->setValue(rate > 0 ? rate * std::max(1, m_duration->value()) : 0);
}

void DataCollectionPage::updateFormState()
{
    const bool running = m_capture && m_capture->running();
    const bool chosen = !m_folder.isEmpty();
    const bool editable = chosen && !running;
    // Папку во время сбора не сменить: выбор папки сбросил бы форму.
    m_path->setEnabled(!running);
    if (QWidget *browse = findChild<QPushButton *>(QStringLiteral("browseButton")))
        browse->setEnabled(!running);
    // Задачу, длительности и состав жестов задаёт продолжаемый набор.
    m_taskCombo->setEnabled(editable && !m_continuing);
    m_prep->setEnabled(editable && !m_continuing);
    m_duration->setEnabled(editable && !m_continuing);
    m_labels->setEnabled(editable && m_descriptorLoaded);
    if (m_addLabelButton)
        m_addLabelButton->setEnabled(editable && m_descriptorLoaded && !m_continuing);
    if (m_removeLabelButton)
        m_removeLabelButton->setEnabled(editable && m_descriptorLoaded && !m_continuing);
    m_targetPlaceholder->setVisible(!m_descriptorLoaded);
    m_targetPlaceholder->setText(m_descriptorLoaded ? QString() : tr("No task is selected: the tasks folder has no readable task descriptor"));
    // Поток запускает только страница устройства; сбор к нему присоединяется.
    const bool streaming = m_context && m_context->session() && m_context->session()->isStreaming();
    m_start->setEnabled(chosen && m_descriptorLoaded && streaming && !running);
}

DatasetSpec DataCollectionPage::buildSpec(const QVector<quint8> &channels, int sampleRate) const
{
    DatasetSpec spec;
    spec.task = m_descriptor;
    spec.task.paradigm.prepSeconds = m_prep->value();
    spec.task.paradigm.recordSeconds = m_duration->value();
    spec.sampleRate = static_cast<uint32_t>(sampleRate);
    spec.channels.reserve(channels.size());
    for (quint8 channel : channels)
        spec.channels.push_back(channel);
    // Одна проба — один пример: окно во всю запись пробы, без перекрытия.
    spec.windowSamples = static_cast<size_t>(sampleRate) *
                         static_cast<size_t>(std::max(1, m_duration->value()));
    // Устройство передаёт целые микровольты, так они и хранятся.
    spec.units = "uV";
    spec.task.capture.channels = spec.channels;
    spec.task.capture.sampleRate = spec.sampleRate;
    spec.task.capture.units = spec.units;
    return spec;
}

bool DataCollectionPage::validateCapture(const DatasetSpec &spec, QString *error) const
{
    const int actualRate = static_cast<int>(spec.sampleRate);
    if (m_descriptor.capture.sampleRate > 0 &&
        m_descriptor.capture.sampleRate != static_cast<uint32_t>(actualRate))
    {
        if (error)
            *error = tr("The descriptor requires %1 Hz, but the device is running at %2 Hz. Change it on the Devices page.")
                         .arg(m_descriptor.capture.sampleRate)
                         .arg(actualRate);
        return false;
    }
    // Каналы выбираются на странице устройства; описание задачи может
    // требовать лишь их число — номера зависят от устройства.
    if (!m_descriptor.capture.channels.empty() &&
        m_descriptor.capture.channels.size() != spec.channels.size())
    {
        if (error)
            *error = tr("The task requires %1 channels, but the stream has %2. Change the selection on the Devices page.")
                         .arg(m_descriptor.capture.channels.size())
                         .arg(spec.channels.size());
        return false;
    }
    if (spec.channels.empty())
    {
        if (error)
            *error = tr("Select channels and start the stream on the Devices page.");
        return false;
    }
    if (spec.windowSamples == 0)
    {
        if (error)
            *error = tr("The window is empty: check the sample rate and the recording time.");
        return false;
    }
    return true;
}

void DataCollectionPage::start()
{
    if (!m_context || !m_capture)
        return;
    auto *liveSession = m_context->session();
    if (!liveSession || !liveSession->isConnected())
    {
        fail(tr("Connect a device first"));
        return;
    }
    if (!m_descriptorLoaded)
    {
        fail(tr("Select a valid task descriptor"));
        return;
    }
    // Путь, введённый без подтверждения, выбирается так же, как кнопкой
    // «Обзор», и форма заполняется заново; сбор не начинается, чтобы оператор
    // её проверил.
    const QString folder = m_path->text().trimmed();
    if (folder != m_folder)
    {
        useFolder(folder);
        if (!m_folder.isEmpty())
            setStatus(m_continuing ? tr("The form is filled from the dataset: check the repetitions and start again")
                                   : tr("The dataset folder is chosen: check the form and start again"));
        return;
    }
    if (m_folder.isEmpty())
    {
        fail(tr("Choose a dataset folder first."));
        return;
    }
    // Каналы — те, что выбраны на странице устройства и по которым идёт поток.
    const QVector<quint8> selected = liveSession->activeChannels();
    DatasetSpec s = buildSpec(selected, liveSession->sampleRateHz());
    // Усиление каналов --- справочное сведение набора.
    for (quint8 channel : selected)
        s.gain.push_back(liveSession->gain(channel));
    QString validationError;
    if (!validateCapture(s, &validationError))
    {
        fail(validationError);
        return;
    }
    // Продолжение набора --- при том же усилении: по нему проверка насыщения
    // считает диапазон АЦП каждой пробы. Расхождение называется сразу, с
    // обоими значениями, а не общим отказом набора.
    if (m_continuing && s.gain != m_continuedGain)
    {
        auto gains = [](const std::vector<int> &values)
        {
            QStringList text;
            for (int value : values)
                text << QStringLiteral("×%1").arg(value);
            return text.join(QStringLiteral(", "));
        };
        fail(tr("The dataset was recorded with gain %1, the stream has gain %2. Change the gain on the Devices page.")
                 .arg(gains(m_continuedGain), gains(s.gain)));
        return;
    }
    // Поток запускает только страница устройства. Сбор данных к нему
    // присоединяется, поэтому требует уже идущей регистрации по тем же
    // каналам, что заданы в спецификации набора.
    if (!liveSession->isStreaming())
    {
        fail(tr("Start the stream on the Devices page, then start capture"));
        return;
    }
    // Build a randomized schedule while avoiding consecutive repetitions whenever
    // another gesture still has samples to capture. If only one gesture remains,
    // consecutive entries are unavoidable and are emitted as-is.
    std::vector<std::pair<std::string, int>> remaining;
    size_t total = 0;
    for (int r = 0; r < m_labels->rowCount(); ++r)
        if (m_labels->item(r, 0) && !m_labels->item(r, 0)->text().isEmpty())
        {
            int reps = m_labels->item(r, 1) ? m_labels->item(r, 1)->text().toInt() : 1;
            int count = std::max(m_continuing ? 0 : 1, reps);
            if (count == 0)
                continue;
            remaining.emplace_back(m_labels->item(r, 0)->text().toStdString(), count);
            total += size_t(count);
        }
    std::vector<std::string> labels;
    labels.reserve(total);
    std::mt19937 rng(std::random_device{}());
    if (!m_descriptor.paradigm.randomize)
    {
        for (const auto &entry : remaining)
            for (int i = 0; i < entry.second; ++i)
                labels.push_back(entry.first);
    }
    std::string previous;
    while (m_descriptor.paradigm.randomize && labels.size() < total)
    {
        // Prefer the most frequent remaining gesture. This is the standard
        // reorganize-string strategy: it keeps the largest class distributed
        // across the schedule and avoids repeats whenever a valid arrangement
        // exists, while randomizing ties for a different order on each run.
        int bestCount = 0;
        for (const auto &entry : remaining)
            if (entry.second > bestCount && entry.first != previous)
                bestCount = entry.second;
        std::vector<size_t> candidates;
        for (size_t i = 0; i < remaining.size(); ++i)
            if (remaining[i].second > 0 && remaining[i].first != previous && remaining[i].second == bestCount)
                candidates.push_back(i);
        if (candidates.empty())
            for (size_t i = 0; i < remaining.size(); ++i)
                if (remaining[i].second > 0)
                    candidates.push_back(i);
        if (candidates.empty())
            break;
        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
        auto &entry = remaining[candidates[pick(rng)]];
        labels.push_back(entry.first);
        previous = entry.first;
        --entry.second;
    }
    if (labels.empty())
    {
        fail(m_continuing ? tr("Set the repetitions of at least one gesture") : tr("Add at least one gesture label"));
        return;
    }
    // Persist the edited vocabulary in the descriptor. The randomized schedule
    // may contain repetitions, while events are kept once per label.
    s.task.events.clear();
    for (int row = 0; row < m_labels->rowCount(); ++row)
    {
        const auto *item = m_labels->item(row, 0);
        if (!item || item->text().trimmed().isEmpty())
            continue;
        const std::string label = item->text().trimmed().toStdString();
        const auto found = std::find_if(s.task.events.begin(), s.task.events.end(), [&](const EventSpec &event)
                                        { return event.target.type == TargetType::Label && event.target.label == label; });
        if (found != s.task.events.end())
            continue;
        EventSpec event;
        event.id = label;
        event.target.type = TargetType::Label;
        event.target.label = label;
        // План повторений сеанса: продолжение набора подставит его снова, и
        // жест, пропущенный в этом сеансе, предложит пропустить и в следующем.
        event.repetitions = std::max(m_continuing ? 0 : 1,
                                     m_labels->item(row, 1) ? m_labels->item(row, 1)->text().toInt() : 1);
        const auto descriptorEvent = std::find_if(m_descriptor.events.begin(), m_descriptor.events.end(),
                                                  [&](const EventSpec &candidate)
                                                  {
                                                      return candidate.target.type == TargetType::Label &&
                                                             candidate.target.label == label;
                                                  });
        event.cueText = descriptorEvent != m_descriptor.events.end() && !descriptorEvent->cueText.empty()
                            ? descriptorEvent->cueText
                            : label;
        s.task.events.push_back(std::move(event));
    }
    // Накопитель читает общий DataHub; геометрия потока уже задана страницей
    // устройства и здесь не меняется.
    const bool append = m_continuing;
    std::string datasetPath;
    if (append)
    {
        datasetPath = folder.toStdString();
    }
    else
    {
        try
        {
            datasetPath = nextDatasetBundlePath(m_folder.toStdString());
        }
        catch (const std::exception &e)
        {
            fail(tr("Unable to create dataset folder: %1").arg(QString::fromUtf8(e.what())));
            return;
        }
    }
    std::string openError;
    if (!m_capture->start(datasetPath, s, labels, append, &openError))
    {
        fail(tr("Unable to open the dataset: %1").arg(errorText(openError)));
        return;
    }
    m_examplesWritten = 0;
    // Во время сбора форма недоступна.
    updateFormState();
    m_stop->setEnabled(true);
    setStatus(append ? tr("Cycle started: continuing %1").arg(m_folder) : tr("Cycle started"));
    disconnect(m_capture.get(), nullptr, this, nullptr);
    connect(m_capture.get(), &QtDatasetCaptureAdapter::cueChanged,
            this, &DataCollectionPage::onCueChanged);
    connect(m_capture.get(), &QtDatasetCaptureAdapter::progress,
            this, &DataCollectionPage::onCaptureProgress);
    connect(m_capture.get(), &QtDatasetCaptureAdapter::completed,
            this, &DataCollectionPage::onCaptureCompleted);
    // Проверка пробы сразу после записи: оператор видит отметку и может
    // вспомнить, что произошло, пока проба свежа в памяти. Подозрительная
    // проба останавливает расписание, пока оператор не решит, повторить её
    // или оставить.
    m_suspiciousTrials.clear();
    m_repeatedTrials.clear();
    m_lastSuspicious = {};
    connect(m_capture.get(), &QtDatasetCaptureAdapter::trialChecked, this,
            [this](int cycle, quint64 id, const QString &label, const QStringList &flagged)
            {
                if (!m_cueDialog)
                    return;
                if (flagged.isEmpty())
                {
                    // Прежняя подозрительная проба ещё ждёт решения оператора.
                    if (m_lastSuspicious.cycle < 0)
                        m_cueDialog->showTrialCheck(QString());
                    return;
                }
                QStringList names;
                for (const QString &check : flagged)
                    names << qualityCheckName(check);
                const QString reasons = names.join(QStringLiteral(", "));
                m_lastSuspicious = {cycle, id, label};
                m_cueDialog->showTrialCheck(tr("Trial %1 (%2) looks suspicious: %3")
                                                .arg(cycle + 1).arg(label, reasons),
                                            true);
                m_suspiciousTrials << tr("No. %1 — %2: %3").arg(id).arg(label, reasons);
            });

    QVector<CueCaptureDialog::CueEntry> cueEntries;
    for (int row = 0; row < m_labels->rowCount(); ++row)
    {
        if (!m_labels->item(row, 0) || m_labels->item(row, 0)->text().trimmed().isEmpty())
            continue;
        CueCaptureDialog::CueEntry entry;
        entry.label = m_labels->item(row, 0)->text().trimmed();
        entry.cueText = entry.label;
        entry.totalRepetitions = std::max(m_continuing ? 0 : 1,
                                          m_labels->item(row, 1) ? m_labels->item(row, 1)->text().toInt() : 1);
        // Жест, пропущенный в этом сеансе, в окне команд не выводится.
        if (entry.totalRepetitions == 0)
            continue;
        for (const auto &event : s.task.events)
            if (event.target.label == entry.label && !event.cueText.empty())
                entry.cueText = QString::fromStdString(event.cueText);
        cueEntries.push_back(std::move(entry));
    }
    QVector<QString> cycleLabels;
    for (const auto &label : labels)
        cycleLabels.push_back(QString::fromStdString(label));
    m_cueDialog = std::make_unique<CueCaptureDialog>(this);
    m_cueDialog->setSchedule(cueEntries, cycleLabels, s.task.paradigm.prepSeconds,
                             s.task.paradigm.recordSeconds);
    connect(m_cueDialog.get(), &CueCaptureDialog::stopRequested,
            this, &DataCollectionPage::stop);
    connect(m_cueDialog.get(), &CueCaptureDialog::repeatRequested, this, [this]
            {
                if (!m_cueDialog || m_lastSuspicious.cycle < 0)
                    return;
                const auto trial = m_lastSuspicious;
                m_lastSuspicious = {};
                if (!m_capture->repeatTrial(trial.cycle, trial.id))
                    return;
                m_cueDialog->addTrial(trial.label);
                m_cueDialog->showTrialCheck(tr("Trial %1 will be recorded again at the end").arg(trial.cycle + 1));
                m_repeatedTrials << tr("No. %1 — %2").arg(trial.id).arg(trial.label);
                m_capture->resume();
            });
    connect(m_cueDialog.get(), &CueCaptureDialog::continueRequested, this, [this]
            {
                m_lastSuspicious = {};
                if (m_cueDialog)
                    m_cueDialog->showTrialCheck(QString());
                m_capture->resume();
            });
    connect(m_capture.get(), &QtDatasetCaptureAdapter::paused, this, [this]
            {
                setStatus(tr("Collection paused: the last trial looks suspicious"));
                if (m_cueDialog)
                    m_cueDialog->showPaused();
            });
    // Экран и разметка идут по одним часам — таймеру проб.
    connect(m_capture.get(), &QtDatasetCaptureAdapter::countdown,
            m_cueDialog.get(), &CueCaptureDialog::showCountdown);
    m_cueDialog->show();
    m_cueDialog->raise();
    m_cueDialog->activateWindow();
    // Расписание запускается последним: первая команда выдаётся сразу, и к
    // этому моменту окно и сигналы уже готовы.
    m_capture->startCues();
}
void DataCollectionPage::stop()
{
    if (m_capture)
        m_capture->stop();
}

void DataCollectionPage::onCueChanged(int cycle, const QString &label, bool recording, int seconds)
{
    setStatus(recording ? tr("Recording %1 (%2 s)").arg(label).arg(seconds)
                        : tr("Preparing %1 (%2 s)").arg(label).arg(seconds));
    if (m_cueDialog)
        m_cueDialog->showCue(cycle, label, recording);
}

void DataCollectionPage::onCaptureProgress(int cycle, quint64 examples)
{
    Q_UNUSED(cycle);
    m_examplesWritten = examples;
}

void DataCollectionPage::onCaptureCompleted(bool stopped)
{
    updateCaptureRateUi();
    m_stop->setEnabled(false);
    if (m_cueDialog)
    {
        m_cueDialog->finish(stopped);
        m_cueDialog.reset();
    }
    setStatus((stopped ? tr("Capture stopped: %1 windows saved")
                       : tr("Capture complete: %1 windows saved"))
                  .arg(m_examplesWritten));
    // Итог проверки --- окном: его нужно заметить до обучения.
    if (!m_suspiciousTrials.isEmpty() && m_context && m_context->notifications())
    {
        QString text = tr("Suspicious trials (numbers in the dataset):\n%1")
                           .arg(m_suspiciousTrials.join(QLatin1Char('\n')));
        if (!m_repeatedTrials.isEmpty())
            text += tr("\n\nRecorded again; the first recordings are excluded:\n%1")
                        .arg(m_repeatedTrials.join(QLatin1Char('\n')));
        text += tr("\n\nLook at the trials on the Datasets page and exclude the ones that went wrong.");
        m_context->notifications()->info(this, tr("Data Collection"), text);
    }
    m_suspiciousTrials.clear();
    m_repeatedTrials.clear();
}
