#ifndef AI_DATASETPAGE_H
#define AI_DATASETPAGE_H

#include "navigation/AiPageBase.h"
#include "ai/datasetindex.h"

#include <QDateTime>
#include <QVector>

class QComboBox;
class QCustomPlot;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSplitter;
class QTableWidget;

// Граничный класс варианта использования «Отбор проб набора данных».
// Показывает пробы с метриками и флагами проверок качества, которые задаёт
// описание задачи (колонки строятся по реестру проверок, страница не знает
// типа задачи), отбирает пробы по классу, состоянию и флагу, выводит сигнал
// пробы, исключает и возвращает пробы, исправляет метку по схеме задачи и
// хранит примечание. Пробы и проверки даёт DatasetIndex.
class DatasetPage : public AiPageBase
{
    Q_OBJECT
public:
    explicit DatasetPage(AppContext *context, QWidget *parent = nullptr);

protected:
    // Набор, пополненный на странице сбора, перечитывается при возврате.
    // Пути наборов между запусками не хранятся: после запуска набор не выбран.
    void showEvent(QShowEvent *event) override;

private slots:
    void chooseDataset();
    void applyFilters();
    void showCurrentTrial();
    void step(int delta);
    void excludeTrials(bool excluded);
    void changeTarget();
    void editNote();

private:
    // Охват массовой операции: выделенные пробы или все пробы с флагом.
    struct MassScope
    {
        bool selected = true;
        QString flag; // id проверки; "*" --- любой флаг
    };

    bool loadDataset(const QString &path, bool reportErrors);
    void fillFilters();
    void fillTable();
    void refreshRows();
    void updateRow(int row);
    void updateSummary();
    void updatePosition();
    void rebuildPlot();
    void applyPlotTheme();
    bool askScope(bool exclude, MassScope &scope);
    // Пробы охвата, к которым применима операция: исключаются пробы,
    // оставшиеся в выборке, возвращаются --- исключённые.
    QVector<size_t> scopeTrials(const MassScope &scope, bool exclude) const;
    QVector<size_t> selectedTrials() const;
    int trialOfRow(int row) const;
    int currentTrial() const;
    QDateTime indexModified() const;
    QString targetText(const TargetValue &target) const;
    QString unitsText() const;

    DatasetIndex m_index;
    bool m_loaded = false;
    QDateTime m_indexModified;
    QLineEdit *m_path = nullptr;
    // Сводка, таблица проб и подробный вид; границы перетаскиваются.
    QSplitter *m_splitter = nullptr;
    // Высота сводки задана по её строкам при первом открытии набора; дальше
    // её меняет оператор.
    bool m_summarySized = false;
    QTableWidget *m_summary = nullptr;
    QComboBox *m_class = nullptr;
    QComboBox *m_state = nullptr;
    QComboBox *m_flag = nullptr;
    QTableWidget *m_table = nullptr;
    QCustomPlot *m_plot = nullptr;
    QLabel *m_detail = nullptr;
    QLabel *m_position = nullptr;
    QPushButton *m_previous = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_exclude = nullptr;
    QPushButton *m_restore = nullptr;
    QPushButton *m_target = nullptr;
    QPushButton *m_note = nullptr;
};

#endif // AI_DATASETPAGE_H
