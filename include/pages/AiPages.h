#ifndef AIPAGES_H
#define AIPAGES_H
#include <QWidget>
#include <QVector>
#include <memory>
#include "core/datasetcapture.h"
#include "core/dataset.h"
#include "core/datahub.h"
class AppContext; class QLabel; class QPushButton; class QLineEdit; class QComboBox; class QSpinBox; class QDoubleSpinBox; class QProgressBar; class QTableWidget; class QCustomPlot; class QCheckBox;
class AiPageBase: public QWidget { public: explicit AiPageBase(AppContext*,const QString&,QWidget *parent=nullptr); protected: AppContext*m_context; QLabel*m_status; };
class DataCollectionPage: public AiPageBase { public: explicit DataCollectionPage(AppContext*,QWidget *parent=nullptr); private slots: void choosePath(); void addLabel(); void removeLabel(); void start(); void stop(); private: void refreshSpec(); QPushButton*m_start; QPushButton*m_stop; QLineEdit*m_path; QLineEdit*m_taskId; QLineEdit*m_taskName; QLineEdit*m_family; QSpinBox*m_prep; QSpinBox*m_duration; QSpinBox*m_repetitions; QTableWidget*m_labels; QVector<QCheckBox*>m_channels; std::unique_ptr<DatasetCaptureController>m_capture; };
class TrainingPage: public AiPageBase { public: explicit TrainingPage(AppContext*,QWidget *parent=nullptr); private slots: void chooseDataset(); void chooseOutput(); void train(); void stop(); void onMetric(int,int,double,double,double); private: QLineEdit*m_dataset; QLineEdit*m_output; QSpinBox*m_epochs; QSpinBox*m_batch; QDoubleSpinBox*m_lr; QDoubleSpinBox*m_val; QComboBox*m_device; QProgressBar*m_progress; QTableWidget*m_metrics; QPushButton*m_train; DatasetSpec m_spec; };
class InferencePage: public AiPageBase { public: explicit InferencePage(AppContext*,QWidget *parent=nullptr); private slots: void chooseModel(); void start(); void stop(); void onBlock(const class DataHub::Block&); private: QLineEdit*m_model; QLabel*m_descriptor; QTableWidget*m_top5; QPushButton*m_start; QPushButton*m_stop; DatasetSpec m_spec; uint64_t m_token=0; QVector<QVector<float>>m_window; uint64_t m_seen=0; };
#endif
