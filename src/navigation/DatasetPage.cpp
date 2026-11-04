#include "navigation/DatasetPage.h"
#include "ai/errortext.h"
#include "navigation/qualitytext.h"
#include "navigation/AppContext.h"
#include "settings/thememanager.h"
#include "ui_datasetpage.h"

#include "qcustomplot.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
    // Справочные колонки до колонок проверок; после них --- примечание.
    enum Column
    {
        NumberColumn,
        ClassColumn,
        TimeColumn,
        StateColumn,
        FlagsColumn,
        FirstCheckColumn
    };

    enum StateFilter
    {
        AllTrials,
        IncludedTrials,
        ExcludedTrials
    };

    constexpr int TrialRole = Qt::UserRole;
    constexpr int SortRole = Qt::UserRole + 1;

    // Ячейка, которая сортируется по числу, а показывает округлённый текст.
    class NumberItem : public QTableWidgetItem
    {
    public:
        NumberItem() { setTextAlignment(Qt::AlignRight | Qt::AlignVCenter); }
        void set(double value, const QString &text)
        {
            setData(SortRole, value);
            setText(text);
        }
        bool operator<(const QTableWidgetItem &other) const override
        {
            return data(SortRole).toDouble() < other.data(SortRole).toDouble();
        }
    };

    // Значение проверки в таблице: для сравнения внутри группы --- оценка |z|,
    // для насыщения --- доля в процентах, для остальных --- измеренная мера.
    // NaN --- значения нет (мало данных, проверка не применима).
    double columnValue(const quality::ConfiguredCheck &check, const quality::Measurement &m,
                       const quality::Judgement &j)
    {
        if (!m.applicable || j.verdict == quality::Verdict::NotApplicable)
            return std::numeric_limits<double>::quiet_NaN();
        if (check.check->scope() == quality::Scope::Conditional)
            return j.verdict == quality::Verdict::InsufficientData ? std::numeric_limits<double>::quiet_NaN() : j.score;
        return qualityDisplayValue(QString::fromStdString(check.check->id()), m.value);
    }
}

DatasetPage::DatasetPage(AppContext *c, QWidget *p) : AiPageBase(c, tr("Datasets"), p)
{
    setObjectName(QStringLiteral("datasets"));
    if (c && c->notifications())
        c->notifications()->attachStatusLabel(objectName(), m_status);
    auto *l = qobject_cast<QVBoxLayout *>(layout());
    auto *content = new QWidget(this);
    Ui::DatasetPageForm form;
    form.setupUi(content);
    m_path = form.datasetPath;
    m_summary = form.summaryTable;
    m_class = form.classFilter;
    m_state = form.stateFilter;
    m_flag = form.flagFilter;
    m_table = form.trialsTable;
    m_plot = form.trialPlot;
    m_detail = form.detailLabel;
    m_position = form.positionLabel;
    m_previous = form.previousButton;
    m_next = form.nextButton;
    m_exclude = form.excludeButton;
    m_restore = form.restoreButton;
    m_target = form.targetButton;
    m_note = form.noteButton;
    // Сводка, таблица проб и подробный вид пробы --- в одном разделителе:
    // размеры всех трёх областей меняет оператор. Таблица проб получает
    // больше места, чем подробный вид.
    m_splitter = form.splitter;
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 3);
    m_splitter->setStretchFactor(2, 2);
    l->insertWidget(2, content, 1);

    m_state->addItem(tr("All"), AllTrials);
    m_state->addItem(tr("In use"), IncludedTrials);
    m_state->addItem(tr("Excluded", "state filter"), ExcludedTrials);

    for (QTableWidget *table : {m_table, m_summary})
    {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->verticalHeader()->setVisible(false);
    }
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_summary->setSelectionMode(QAbstractItemView::NoSelection);

    m_plot->plotLayout()->clear();
    if (c && c->themeManager())
        connect(c->themeManager(), &ThemeManager::themeChanged, this, [this](AppTheme) { applyPlotTheme(); });

    connect(form.browseButton, &QPushButton::clicked, this, &DatasetPage::chooseDataset);
    for (QComboBox *filter : {m_class, m_state, m_flag})
        connect(filter, qOverload<int>(&QComboBox::currentIndexChanged), this, &DatasetPage::applyFilters);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DatasetPage::showCurrentTrial);
    connect(m_previous, &QPushButton::clicked, this, [this] { step(-1); });
    connect(m_next, &QPushButton::clicked, this, [this] { step(1); });
    connect(m_exclude, &QPushButton::clicked, this, [this] { excludeTrials(true); });
    connect(m_restore, &QPushButton::clicked, this, [this] { excludeTrials(false); });
    connect(m_target, &QPushButton::clicked, this, &DatasetPage::changeTarget);
    connect(m_note, &QPushButton::clicked, this, &DatasetPage::editNote);
    updatePosition();
    // Пустой график тоже в цветах темы, а не белый.
    applyPlotTheme();
}

void DatasetPage::showEvent(QShowEvent *event)
{
    AiPageBase::showEvent(event);
    if (m_loaded && indexModified() != m_indexModified)
        loadDataset(QString::fromStdString(m_index.directory()), true);
}

void DatasetPage::chooseDataset()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select dataset"), m_path->text());
    if (!dir.isEmpty())
        loadDataset(dir, true);
}

QDateTime DatasetPage::indexModified() const
{
    return QFileInfo(QDir(QString::fromStdString(m_index.directory())).filePath(QStringLiteral("examples.jsonl")))
        .lastModified();
}

QString DatasetPage::targetText(const TargetValue &target) const
{
    // Точка расширения по виду цели (isSupportedTask()): пока только метка.
    return target.type == TargetType::Label ? QString::fromStdString(target.label) : QStringLiteral("—");
}

bool DatasetPage::loadDataset(const QString &path, bool reportErrors)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    DatasetIndex index;
    std::string error;
    const bool ok = index.open(path.toStdString(), &error) && index.evaluate(&error);
    QApplication::restoreOverrideCursor();
    if (!ok)
    {
        if (reportErrors)
            fail(tr("Cannot open the dataset: %1").arg(errorText(error)));
        return false;
    }
    m_index = std::move(index);
    m_loaded = true;
    m_indexModified = indexModified();
    m_path->setText(path);
    fillFilters();
    fillTable();
    rebuildPlot();
    applyFilters();
    updateSummary();
    setStatus(tr("Loaded %n trial(s)", nullptr, static_cast<int>(m_index.trials().size())));
    if (!m_index.config().skipped.empty() && reportErrors)
    {
        QStringList ids;
        for (const auto &id : m_index.config().skipped)
            ids << QString::fromStdString(id);
        fail(tr("The task names checks this version does not run: %1").arg(ids.join(QStringLiteral(", "))));
    }
    return true;
}

void DatasetPage::fillFilters()
{
    const QString cls = m_class->currentData().toString();
    const QString flag = m_flag->currentData().toString();
    const QSignalBlocker classBlocker(m_class);
    const QSignalBlocker flagBlocker(m_flag);
    m_class->clear();
    m_class->addItem(tr("All"), QString());
    QStringList classes;
    for (const auto &event : m_index.spec().task.events)
        if (!classes.contains(targetText(event.target)))
            classes << targetText(event.target);
    for (const auto &trial : m_index.trials())
        if (!classes.contains(targetText(trial.target)))
            classes << targetText(trial.target);
    for (const QString &name : classes)
        m_class->addItem(name, name);
    m_flag->clear();
    m_flag->addItem(tr("All"), QString());
    m_flag->addItem(tr("Any flag"), QStringLiteral("*"));
    for (const auto &check : m_index.config().checks)
    {
        const QString id = QString::fromStdString(check.check->id());
        m_flag->addItem(qualityCheckName(id), id);
    }
    m_flag->addItem(tr("Too few trials to judge"), QStringLiteral("?"));
    // Отбор сохраняется, если в перечитанном наборе он ещё имеет смысл.
    m_class->setCurrentIndex(std::max(0, m_class->findData(cls)));
    m_flag->setCurrentIndex(std::max(0, m_flag->findData(flag)));
}

QString DatasetPage::unitsText() const
{
    const QString units = QString::fromStdString(m_index.spec().units);
    return units == QStringLiteral("uV") ? tr("µV") : units;
}

void DatasetPage::fillTable()
{
    const QSignalBlocker blocker(m_table);
    const QString units = unitsText();
    const auto &checks = m_index.config().checks;
    QStringList headers{tr("No."), tr("Class"), tr("Recorded"), tr("State"), tr("Flags")};
    for (const auto &check : checks)
        headers << qualityColumnTitle(QString::fromStdString(check.check->id()), units);
    headers << tr("Note");
    m_table->setSortingEnabled(false);
    m_table->clear();
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    for (size_t c = 0; c < checks.size(); ++c)
        m_table->horizontalHeaderItem(FirstCheckColumn + int(c))
            ->setToolTip(qualityCheckDescription(QString::fromStdString(checks[c].check->id())));
    m_table->setRowCount(static_cast<int>(m_index.trials().size()));
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        auto *number = new NumberItem;
        const auto &trial = m_index.trials()[static_cast<size_t>(row)];
        number->set(static_cast<double>(trial.id), QString::number(trial.id));
        number->setData(TrialRole, row);
        m_table->setItem(row, NumberColumn, number);
        for (int column = ClassColumn; column < m_table->columnCount(); ++column)
        {
            const bool numeric = column >= FirstCheckColumn && column < FirstCheckColumn + int(checks.size());
            m_table->setItem(row, column, numeric ? new NumberItem : new QTableWidgetItem);
        }
        updateRow(row);
    }
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(NumberColumn, Qt::AscendingOrder);
    m_table->resizeColumnsToContents();
}

void DatasetPage::refreshRows()
{
    const QSignalBlocker blocker(m_table);
    m_table->setSortingEnabled(false);
    for (int row = 0; row < m_table->rowCount(); ++row)
        updateRow(row);
    m_table->setSortingEnabled(true);
}

void DatasetPage::updateRow(int row)
{
    const int index = trialOfRow(row);
    if (index < 0)
        return;
    const auto &trial = m_index.trials()[static_cast<size_t>(index)];
    const auto &checks = m_index.config().checks;
    // Исправленная метка видна вместе с предъявленной командой.
    m_table->item(row, ClassColumn)->setText(trial.relabeled()
                                                 ? tr("%1 (cue: %2)").arg(targetText(trial.target), targetText(trial.cue))
                                                 : targetText(trial.target));
    const QDateTime recorded = QDateTime::fromString(QString::fromStdString(trial.recordedAt), Qt::ISODate);
    m_table->item(row, TimeColumn)->setText(recorded.isValid() ? recorded.time().toString(QStringLiteral("HH:mm:ss"))
                                                               : QString::fromStdString(trial.recordedAt));
    m_table->item(row, TimeColumn)->setToolTip(QString::fromStdString(trial.recordedAt));
    m_table->item(row, StateColumn)->setText(trial.excluded ? tr("Excluded") : tr("In use"));
    QStringList flags;
    for (size_t c = 0; c < checks.size() && c < trial.judgements.size(); ++c)
    {
        const QString id = QString::fromStdString(checks[c].check->id());
        if (trial.judgements[c].verdict == quality::Verdict::Flagged)
            flags << qualityCheckName(id);
        else if (trial.judgements[c].verdict == quality::Verdict::InsufficientData)
            flags << tr("too few trials (%1)").arg(qualityCheckName(id));
    }
    m_table->item(row, FlagsColumn)->setText(flags.join(QStringLiteral(", ")));
    for (size_t c = 0; c < checks.size(); ++c)
    {
        auto *item = static_cast<NumberItem *>(m_table->item(row, FirstCheckColumn + int(c)));
        const double value = c < trial.measurements.size() && c < trial.judgements.size()
                                 ? columnValue(checks[c], trial.measurements[c], trial.judgements[c])
                                 : std::numeric_limits<double>::quiet_NaN();
        const bool none = std::isnan(value);
        item->set(none ? -1.0 : value, none ? QStringLiteral("—") : QString::number(value, 'f', value < 10 ? 2 : 1));
        QFont font = item->font();
        font.setBold(c < trial.judgements.size() && trial.judgements[c].verdict == quality::Verdict::Flagged);
        item->setFont(font);
    }
    m_table->item(row, m_table->columnCount() - 1)->setText(QString::fromStdString(trial.note));
    // Исключённые пробы --- серые, читаемые в обеих темах; остальные берут
    // цвет текста темы.
    for (int column = 0; column < m_table->columnCount(); ++column)
    {
        QTableWidgetItem *item = m_table->item(row, column);
        if (trial.excluded)
            item->setForeground(QColor(128, 133, 143));
        else
            item->setData(Qt::ForegroundRole, QVariant());
    }
}

int DatasetPage::trialOfRow(int row) const
{
    const QTableWidgetItem *item = m_table->item(row, NumberColumn);
    return item ? item->data(TrialRole).toInt() : -1;
}

int DatasetPage::currentTrial() const
{
    const int row = m_table->currentRow();
    return row >= 0 && !m_table->isRowHidden(row) ? trialOfRow(row) : -1;
}

void DatasetPage::applyFilters()
{
    const QString cls = m_class->currentData().toString();
    const int state = m_state->currentData().toInt();
    const QString flag = m_flag->currentData().toString();
    const auto &checks = m_index.config().checks;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        const int index = trialOfRow(row);
        if (index < 0)
            continue;
        const auto &trial = m_index.trials()[static_cast<size_t>(index)];
        bool visible = cls.isEmpty() || targetText(trial.target) == cls;
        if (state == IncludedTrials)
            visible = visible && !trial.excluded;
        else if (state == ExcludedTrials)
            visible = visible && trial.excluded;
        if (flag == QLatin1String("*"))
            visible = visible && trial.flagged();
        else if (flag == QLatin1String("?"))
            visible = visible && std::any_of(trial.judgements.begin(), trial.judgements.end(),
                                             [](const quality::Judgement &j)
                                             { return j.verdict == quality::Verdict::InsufficientData; });
        else if (!flag.isEmpty())
        {
            bool found = false;
            for (size_t c = 0; c < checks.size() && c < trial.judgements.size(); ++c)
                found = found || (QString::fromStdString(checks[c].check->id()) == flag &&
                                  trial.judgements[c].verdict == quality::Verdict::Flagged);
            visible = visible && found;
        }
        m_table->setRowHidden(row, !visible);
    }
    // Выбранная проба, скрытая отбором, сменяется первой видимой.
    const int current = m_table->currentRow();
    if (current < 0 || m_table->isRowHidden(current))
    {
        m_table->clearSelection();
        for (int row = 0; row < m_table->rowCount(); ++row)
            if (!m_table->isRowHidden(row))
            {
                m_table->selectRow(row);
                break;
            }
    }
    showCurrentTrial();
}

void DatasetPage::step(int delta)
{
    int row = m_table->currentRow();
    do
        row += delta;
    while (row >= 0 && row < m_table->rowCount() && m_table->isRowHidden(row));
    if (row < 0 || row >= m_table->rowCount())
        return;
    m_table->selectRow(row);
    m_table->scrollToItem(m_table->item(row, NumberColumn));
}

QVector<size_t> DatasetPage::selectedTrials() const
{
    QVector<size_t> trials;
    QSet<int> rows;
    for (const QTableWidgetItem *item : m_table->selectedItems())
        rows.insert(item->row());
    for (int row : rows)
        if (!m_table->isRowHidden(row) && trialOfRow(row) >= 0)
            trials.push_back(static_cast<size_t>(trialOfRow(row)));
    return trials;
}

QVector<size_t> DatasetPage::scopeTrials(const MassScope &scope, bool exclude) const
{
    QVector<size_t> candidates;
    if (scope.selected)
        candidates = selectedTrials();
    else
    {
        // «Все с флагом» --- по всему набору, а не по отбору в таблице.
        const auto &checks = m_index.config().checks;
        for (size_t t = 0; t < m_index.trials().size(); ++t)
        {
            const auto &trial = m_index.trials()[t];
            bool match = false;
            for (size_t c = 0; c < checks.size() && c < trial.judgements.size(); ++c)
                match = match || (trial.judgements[c].verdict == quality::Verdict::Flagged &&
                                  (scope.flag == QLatin1String("*") ||
                                   QString::fromStdString(checks[c].check->id()) == scope.flag));
            if (match)
                candidates.push_back(t);
        }
    }
    QVector<size_t> trials;
    for (size_t t : candidates)
        if (m_index.trials()[t].excluded != exclude)
            trials.push_back(t);
    return trials;
}

bool DatasetPage::askScope(bool exclude, MassScope &scope)
{
    const int selectedCount = scopeTrials(MassScope{true, QString()}, exclude).size();
    QDialog dialog(this);
    dialog.setWindowTitle(exclude ? tr("Exclude trials") : tr("Return trials"));
    auto *form = new QFormLayout(&dialog);
    auto *selected = new QRadioButton(tr("Selected trials (%1)").arg(selectedCount), &dialog);
    auto *flagged = new QRadioButton(tr("All trials with the flag:"), &dialog);
    auto *flagCombo = new QComboBox(&dialog);
    flagCombo->addItem(tr("any flag"), QStringLiteral("*"));
    for (const auto &check : m_index.config().checks)
    {
        const QString id = QString::fromStdString(check.check->id());
        flagCombo->addItem(qualityCheckName(id), id);
    }
    selected->setEnabled(selectedCount > 0);
    (selectedCount > 0 ? selected : flagged)->setChecked(true);
    form->addRow(selected);
    form->addRow(flagged, flagCombo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    scope.selected = selected->isChecked();
    scope.flag = flagCombo->currentData().toString();
    return true;
}

void DatasetPage::excludeTrials(bool excluded)
{
    if (!m_loaded)
    {
        fail(tr("Select a dataset first"));
        return;
    }
    MassScope scope;
    if (!askScope(excluded, scope))
        return;
    const QVector<size_t> trials = scopeTrials(scope, excluded);
    if (trials.isEmpty())
    {
        fail(tr("No trials match the choice"));
        return;
    }
    std::string error;
    if (!m_index.setExcluded(std::vector<size_t>(trials.begin(), trials.end()), excluded, &error))
    {
        fail(tr("Cannot change the dataset: %1").arg(errorText(error)));
        return;
    }
    m_indexModified = indexModified();
    refreshRows();
    updateSummary();
    setStatus(excluded ? tr("Excluded %n trial(s)", nullptr, static_cast<int>(trials.size()))
                       : tr("Returned %n trial(s)", nullptr, static_cast<int>(trials.size())));
    applyFilters();
}

void DatasetPage::changeTarget()
{
    const int index = currentTrial();
    if (!m_loaded || index < 0)
    {
        fail(tr("Select a trial in the table first"));
        return;
    }
    const auto &trial = m_index.trials()[static_cast<size_t>(index)];
    const auto &task = m_index.spec().task;
    TargetValue target = trial.target;
    bool ok = false;
    // Редактор метки строится по описанию задачи: для метки --- перечень её
    // значений. Это точка расширения по виду цели (isSupportedTask()).
    if (task.targetType == TargetType::Label)
    {
        QStringList values;
        for (const auto &event : task.events)
            if (event.target.type == TargetType::Label && !values.contains(QString::fromStdString(event.target.label)))
                values << QString::fromStdString(event.target.label);
        const QString chosen = QInputDialog::getItem(this, tr("Change label"),
                                                     tr("Target value of trial %1 (cue: %2):")
                                                         .arg(trial.id)
                                                         .arg(targetText(trial.cue)),
                                                     values, std::max(0, int(values.indexOf(targetText(trial.target)))),
                                                     false, &ok);
        target.type = TargetType::Label;
        target.label = chosen.toStdString();
    }
    else
    {
        fail(tr("Targets of this task type cannot be edited"));
        return;
    }
    if (!ok)
        return;
    std::string error;
    if (!m_index.setTarget(static_cast<size_t>(index), target, &error))
    {
        fail(tr("Cannot change the dataset: %1").arg(errorText(error)));
        return;
    }
    m_indexModified = indexModified();
    // Проба перешла в другую группу: флаги сравнения внутри групп меняются.
    fillFilters();
    refreshRows();
    updateSummary();
    applyFilters();
}

void DatasetPage::editNote()
{
    const int index = currentTrial();
    if (!m_loaded || index < 0)
    {
        fail(tr("Select a trial in the table first"));
        return;
    }
    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(
        this, tr("Note"), tr("Note to trial %1:").arg(m_index.trials()[static_cast<size_t>(index)].id),
        QString::fromStdString(m_index.trials()[static_cast<size_t>(index)].note), &ok);
    if (!ok)
        return;
    std::string error;
    if (!m_index.setNote(static_cast<size_t>(index), note.trimmed().toStdString(), &error))
    {
        fail(tr("Cannot change the dataset: %1").arg(errorText(error)));
        return;
    }
    m_indexModified = indexModified();
    refreshRows();
}

void DatasetPage::updateSummary()
{
    // По классам: всего, в выборке, исключено, проб с флагами и число флагов
    // каждой проверки; «мало данных», если проверке не с чем сравнить пробы
    // класса.
    const auto &checks = m_index.config().checks;
    QStringList headers{tr("Class"), tr("Trials"), tr("In use"), tr("Excluded"), tr("Flagged")};
    for (const auto &check : checks)
        headers << qualityCheckName(QString::fromStdString(check.check->id()));
    QStringList classes;
    for (int i = 1; i < m_class->count(); ++i)
        classes << m_class->itemData(i).toString();
    m_summary->clear();
    m_summary->setColumnCount(headers.size());
    m_summary->setHorizontalHeaderLabels(headers);
    for (size_t c = 0; c < checks.size(); ++c)
        m_summary->horizontalHeaderItem(5 + int(c))
            ->setToolTip(qualityCheckDescription(QString::fromStdString(checks[c].check->id())));
    m_summary->setRowCount(classes.size() + 1);
    for (int row = 0; row <= classes.size(); ++row)
    {
        const bool all = row == classes.size();
        std::vector<int> counts(4 + checks.size(), 0);
        std::vector<int> tooFew(checks.size(), 0);
        for (const auto &trial : m_index.trials())
        {
            if (!all && targetText(trial.target) != classes[row])
                continue;
            ++counts[0];
            ++counts[trial.excluded ? 2 : 1];
            counts[3] += trial.flagged() ? 1 : 0;
            for (size_t c = 0; c < checks.size() && c < trial.judgements.size(); ++c)
            {
                counts[4 + c] += trial.judgements[c].verdict == quality::Verdict::Flagged ? 1 : 0;
                tooFew[c] += trial.judgements[c].verdict == quality::Verdict::InsufficientData ? 1 : 0;
            }
        }
        m_summary->setItem(row, 0, new QTableWidgetItem(all ? tr("Total") : classes[row]));
        for (size_t i = 0; i < counts.size(); ++i)
        {
            const bool unjudged = i >= 4 && counts[0] > 0 && tooFew[i - 4] == counts[0];
            auto *item = new QTableWidgetItem(unjudged ? tr("too few data") : QString::number(counts[i]));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (unjudged)
                item->setToolTip(tr("Fewer trials than the check needs to compare them"));
            m_summary->setItem(row, int(i) + 1, item);
        }
        if (all)
            for (int column = 0; column < m_summary->columnCount(); ++column)
            {
                QFont font = m_summary->item(row, column)->font();
                font.setBold(true);
                m_summary->item(row, column)->setFont(font);
            }
    }
    m_summary->resizeColumnsToContents();
    // При первом открытии набора сводке отводится высота её строк, чтобы она
    // была видна целиком, пока классов немного; дальше высоту меняет оператор.
    m_summary->resizeRowsToContents();
    const int rows = std::min(m_summary->rowCount(), 9);
    int height = m_summary->horizontalHeader()->height() + 2 * m_summary->frameWidth();
    for (int row = 0; row < rows; ++row)
        height += m_summary->rowHeight(row);
    if (m_summary->horizontalHeader()->length() > m_summary->viewport()->width())
        height += m_summary->horizontalScrollBar()->sizeHint().height();
    const QList<int> sizes = m_splitter->sizes();
    const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
    if (!m_summarySized && sizes.size() == 3 && total > height)
    {
        m_summarySized = true;
        const int rest = total - height;
        m_splitter->setSizes({height, rest * 3 / 5, rest - rest * 3 / 5});
    }
}

void DatasetPage::updatePosition()
{
    int visible = 0;
    int position = 0;
    const int current = m_table->currentRow();
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        if (m_table->isRowHidden(row))
            continue;
        ++visible;
        if (row == current)
            position = visible;
    }
    m_position->setText(visible ? tr("Trial %1 of %2").arg(position).arg(visible) : tr("No trials"));
    for (QPushButton *button : {m_previous, m_next, m_target, m_note})
        button->setEnabled(visible > 0);
    m_exclude->setEnabled(m_loaded);
    // Вернуть можно только исключённую пробу: кнопка доступна, если среди
    // выделенных проб есть исключённая.
    m_restore->setEnabled(m_loaded && !scopeTrials(MassScope{true, QString()}, false).isEmpty());
}

void DatasetPage::rebuildPlot()
{
    // Каналы --- друг под другом, с общей осью времени.
    m_plot->clearGraphs();
    m_plot->clearItems();
    m_plot->plotLayout()->clear();
    const size_t channels = m_index.spec().channels.size();
    QCPAxisRect *previous = nullptr;
    for (size_t c = 0; c < channels; ++c)
    {
        auto *rect = new QCPAxisRect(m_plot);
        m_plot->plotLayout()->addElement(static_cast<int>(c), 0, rect);
        rect->axis(QCPAxis::atLeft)->setLabel(tr("Ch. %1").arg(c + 1));
        rect->axis(QCPAxis::atBottom)->setTickLabels(c + 1 == channels);
        if (c + 1 == channels)
            rect->axis(QCPAxis::atBottom)->setLabel(tr("Time (s)"));
        if (previous)
            connect(previous->axis(QCPAxis::atBottom), qOverload<const QCPRange &>(&QCPAxis::rangeChanged),
                    rect->axis(QCPAxis::atBottom), qOverload<const QCPRange &>(&QCPAxis::setRange));
        m_plot->addGraph(rect->axis(QCPAxis::atBottom), rect->axis(QCPAxis::atLeft));
        // Граница устойчивой части: проверки и обучение начинаются после неё.
        auto *onset = new QCPItemStraightLine(m_plot);
        onset->setClipAxisRect(rect);
        onset->point1->setAxes(rect->axis(QCPAxis::atBottom), rect->axis(QCPAxis::atLeft));
        onset->point2->setAxes(rect->axis(QCPAxis::atBottom), rect->axis(QCPAxis::atLeft));
        onset->point1->setCoords(m_index.config().skipSeconds, 0.0);
        onset->point2->setCoords(m_index.config().skipSeconds, 1.0);
        onset->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
        previous = rect;
    }
    applyPlotTheme();
}

void DatasetPage::showCurrentTrial()
{
    updatePosition();
    const int index = currentTrial();
    std::vector<std::vector<double>> signal;
    std::string error;
    if (index < 0 || !m_index.signal(static_cast<size_t>(index), signal, &error))
    {
        for (int g = 0; g < m_plot->graphCount(); ++g)
            m_plot->graph(g)->data()->clear();
        m_plot->replot(QCustomPlot::rpQueuedReplot);
        m_detail->clear();
        if (index >= 0)
            fail(tr("Cannot read the trial: %1").arg(errorText(error)));
        return;
    }
    const double rate = m_index.spec().sampleRate > 0 ? m_index.spec().sampleRate : 1.0;
    const auto &trial = m_index.trials()[static_cast<size_t>(index)];
    const auto &checks = m_index.config().checks;
    const QString units = unitsText();
    QStringList lines;
    for (int g = 0; g < m_plot->graphCount() && static_cast<size_t>(g) < signal.size(); ++g)
    {
        const auto &channel = signal[static_cast<size_t>(g)];
        QVector<double> x(static_cast<int>(channel.size()));
        QVector<double> y(static_cast<int>(channel.size()));
        for (int s = 0; s < x.size(); ++s)
        {
            x[s] = s / rate;
            y[s] = channel[static_cast<size_t>(s)];
        }
        m_plot->graph(g)->setData(x, y, true);
        m_plot->graph(g)->rescaleAxes();
        // Подробно по каналу: меры проверок, среди них СКЗ устойчивой части.
        QStringList parts;
        for (size_t c = 0; c < checks.size() && c < trial.measurements.size(); ++c)
            if (trial.measurements[c].applicable && static_cast<size_t>(g) < trial.measurements[c].channels.size())
                parts << qualityMeasureText(QString::fromStdString(checks[c].check->id()),
                                            trial.measurements[c].channels[static_cast<size_t>(g)], units);
        lines << tr("Ch. %1: %2").arg(g + 1).arg(parts.join(QStringLiteral(" · ")));
    }
    m_detail->setText(lines.join(QLatin1Char('\n')));
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DatasetPage::applyPlotTheme()
{
    // Те же цвета, что у графиков формы просмотра.
    const bool dark = !m_context || !m_context->themeManager() || m_context->themeManager()->isDark();
    const QColor background = dark ? QColor(24, 26, 31) : QColor(255, 255, 255);
    const QColor foreground = dark ? QColor(210, 214, 222) : QColor(45, 48, 55);
    const QColor grid = dark ? QColor(65, 70, 80) : QColor(205, 210, 218);
    m_plot->setBackground(background);
    for (QCPAxisRect *rect : m_plot->axisRects())
        for (QCPAxis *axis : rect->axes())
        {
            axis->setBasePen(QPen(foreground));
            axis->setTickPen(QPen(foreground));
            axis->setSubTickPen(QPen(foreground));
            axis->setTickLabelColor(foreground);
            axis->setLabelColor(foreground);
            axis->grid()->setPen(QPen(grid));
            axis->grid()->setSubGridPen(QPen(grid));
        }
    for (int g = 0; g < m_plot->graphCount(); ++g)
        m_plot->graph(g)->setPen(QPen(dark ? QColor(90, 170, 240) : QColor(0, 90, 160)));
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}
