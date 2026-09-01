#include "pages/AiPages.h"
#include "app/AppContext.h"
#include "adapters/qtaiworkeradapter.h"
#include "adapters/qtdevicesessionadapter.h"
#include "core/aimodel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QTableWidgetItem>
#include <algorithm>
#include <random>

AiPageBase::AiPageBase(AppContext*c,const QString&t,QWidget*p):QWidget(p),m_context(c){auto*l=new QVBoxLayout(this);l->addWidget(new QLabel("AI / "+t,this));m_status=new QLabel(tr("Ready"),this);l->addWidget(m_status);}

DataCollectionPage::DataCollectionPage(AppContext*c,QWidget*p):AiPageBase(c,"Data Collection",p){auto*l=qobject_cast<QVBoxLayout*>(layout());auto*f=new QFormLayout; m_path=new QLineEdit("dataset.bset",this);auto*pb=new QPushButton(tr("Browse..."),this);auto*pr=new QHBoxLayout;pr->addWidget(m_path);pr->addWidget(pb);f->addRow(tr("Dataset folder:"),pr);m_taskId=new QLineEdit("gesture-classification",this);f->addRow(tr("Task id:"),m_taskId);m_taskName=new QLineEdit("Gesture classification",this);f->addRow(tr("Task name:"),m_taskName);m_family=new QLineEdit("custom",this);f->addRow(tr("Model family:"),m_family);m_prep=new QSpinBox(this);m_prep->setRange(0,60);m_prep->setValue(5);f->addRow(tr("Preparation (s):"),m_prep);m_duration=new QSpinBox(this);m_duration->setRange(1,120);m_duration->setValue(7);f->addRow(tr("Recording (s):"),m_duration);m_repetitions=new QSpinBox(this);m_repetitions->setRange(1,100);m_repetitions->setValue(1);f->addRow(tr("Repetitions per label:"),m_repetitions);l->insertLayout(1,f);m_labels=new QTableWidget(0,2,this);m_labels->setHorizontalHeaderLabels({tr("Label"),tr("Repetitions")});m_labels->horizontalHeader()->setStretchLastSection(true);l->insertWidget(2,m_labels);auto*lr=new QHBoxLayout;auto*add=new QPushButton(tr("Add label"),this);auto*rem=new QPushButton(tr("Remove label"),this);lr->addWidget(add);lr->addWidget(rem);lr->addStretch();l->insertLayout(3,lr);auto*cr=new QHBoxLayout;m_start=new QPushButton(tr("Start cycle"),this);m_stop=new QPushButton(tr("Stop"),this);m_stop->setEnabled(false);cr->addWidget(m_start);cr->addWidget(m_stop);l->insertLayout(4,cr);addLabel();connect(pb,&QPushButton::clicked,this,&DataCollectionPage::choosePath);connect(add,&QPushButton::clicked,this,&DataCollectionPage::addLabel);connect(rem,&QPushButton::clicked,this,&DataCollectionPage::removeLabel);connect(m_start,&QPushButton::clicked,this,&DataCollectionPage::start);connect(m_stop,&QPushButton::clicked,this,&DataCollectionPage::stop);if(c)m_capture=std::make_unique<DatasetCaptureController>(c->dataHub(),this);}
void DataCollectionPage::choosePath(){auto d=QFileDialog::getExistingDirectory(this,tr("Select dataset folder"),m_path->text());if(!d.isEmpty())m_path->setText(d);}
void DataCollectionPage::addLabel(){int r=m_labels->rowCount();m_labels->insertRow(r);m_labels->setItem(r,0,new QTableWidgetItem(QString("gesture_%1").arg(r+1)));m_labels->setItem(r,1,new QTableWidgetItem("1"));}
void DataCollectionPage::removeLabel(){if(m_labels->currentRow()>=0)m_labels->removeRow(m_labels->currentRow());}
void DataCollectionPage::refreshSpec(){ }
void DataCollectionPage::start(){if(!m_context||!m_capture)return;if(!m_context->acquireMode(AppContext::AcquisitionMode::DatasetCapture)){m_status->setText(tr("Device is busy with another mode"));return;}auto *liveSession=m_context->session();if(!liveSession||!liveSession->isConnected()){m_context->releaseMode(AppContext::AcquisitionMode::DatasetCapture);m_status->setText(tr("Connect a device first"));return;}liveSession->stopStream();DatasetSpec s;s.taskId=m_taskId->text().toStdString();s.displayName=m_taskName->text().toStdString();s.modelFamily=m_family->text().toStdString();s.sampleRate=liveSession->sampleRateHz();s.windowSamples=size_t(s.sampleRate*m_duration->value());s.strideSamples=s.windowSamples;const int channelCount=std::max(1,std::min(8,liveSession->channelCount()));for(int c=0;c<channelCount;++c)s.channels.push_back(static_cast<uint8_t>(c));
    // Build a randomized schedule while avoiding consecutive repetitions whenever
    // another gesture still has samples to capture. If only one gesture remains,
    // consecutive entries are unavoidable and are emitted as-is.
    std::vector<std::pair<std::string,int>> remaining;
    size_t total=0;
    for(int r=0;r<m_labels->rowCount();++r)if(m_labels->item(r,0)&&!m_labels->item(r,0)->text().isEmpty()){int reps=m_labels->item(r,1)?m_labels->item(r,1)->text().toInt():1;int count=std::max(1,reps*m_repetitions->value());remaining.emplace_back(m_labels->item(r,0)->text().toStdString(),count);total+=size_t(count);}
    std::vector<std::string> labels;labels.reserve(total);std::mt19937 rng(std::random_device{}());std::string previous;
    while(labels.size()<total){
        // Prefer the most frequent remaining gesture. This is the standard
        // reorganize-string strategy: it keeps the largest class distributed
        // across the schedule and avoids repeats whenever a valid arrangement
        // exists, while randomizing ties for a different order on each run.
        int bestCount=0;for(const auto&entry:remaining)if(entry.second>bestCount&&entry.first!=previous)bestCount=entry.second;
        std::vector<size_t> candidates;for(size_t i=0;i<remaining.size();++i)if(remaining[i].second>0&&remaining[i].first!=previous&&remaining[i].second==bestCount)candidates.push_back(i);
        if(candidates.empty())for(size_t i=0;i<remaining.size();++i)if(remaining[i].second>0)candidates.push_back(i);if(candidates.empty())break;
        std::uniform_int_distribution<size_t> pick(0,candidates.size()-1);auto&entry=remaining[candidates[pick(rng)]];labels.push_back(entry.first);previous=entry.first;--entry.second;
    }
    if(labels.empty()){m_context->releaseMode(AppContext::AcquisitionMode::DatasetCapture);m_status->setText(tr("Add at least one gesture label"));return;}
    // Persist the task's label vocabulary in the dataset descriptor. The
    // randomized schedule may contain repetitions, so keep one stable entry
    // per class while preserving first-seen order.
    for (const auto &label : labels) {
        if (std::find(s.labels.begin(), s.labels.end(), label) == s.labels.end()) {
            s.labels.push_back(label);
            s.labelSpecs.push_back({label, label});
        }
    }
    if(!m_capture->start(m_path->text().toStdString(),s,labels,1,m_prep->value(),m_duration->value())){m_context->releaseMode(AppContext::AcquisitionMode::DatasetCapture);m_status->setText(tr("Unable to open dataset"));return;}
    // The capture controller consumes the shared DataHub, so ensure the live
    // device is actually streaming with the same channel geometry first.
    if (auto *session = m_context->session()) {
        QVector<quint8> channels;
        for (auto c : s.channels) channels.push_back(c);
        // Firmware emits one complete sample set (all selected channels) per
        // Push; payload size is derived from this channel list.
        session->startStream(channels);
    }
    m_start->setEnabled(false);m_stop->setEnabled(true);m_status->setText(tr("Cycle started"));disconnect(m_capture.get(),nullptr,this,nullptr);connect(m_capture.get(),&DatasetCaptureController::progress,this,[this](int li,int rep,double sec,const QString&label){m_status->setText(tr("Recording %1, repetition %2 (%3 s)").arg(label).arg(rep+1).arg(sec,0,'f',1));});connect(m_capture.get(),&DatasetCaptureController::completed,this,[this](bool stopped){m_start->setEnabled(true);m_stop->setEnabled(false);if(m_context){if(m_context->session())m_context->session()->stopStream();m_context->releaseMode(AppContext::AcquisitionMode::DatasetCapture);}m_status->setText(stopped?tr("Capture stopped"):tr("Capture complete"));});}
void DataCollectionPage::stop(){if(m_capture)m_capture->stop();}

TrainingPage::TrainingPage(AppContext*c,QWidget*p):AiPageBase(c,"Training",p){auto*l=qobject_cast<QVBoxLayout*>(layout());auto*f=new QFormLayout;m_dataset=new QLineEdit(this);auto*db=new QPushButton(tr("Browse..."),this);auto*dr=new QHBoxLayout;dr->addWidget(m_dataset);dr->addWidget(db);f->addRow(tr("Dataset (.bset):"),dr);m_output=new QLineEdit("models",this);auto*ob=new QPushButton(tr("Browse..."),this);auto*orr=new QHBoxLayout;orr->addWidget(m_output);orr->addWidget(ob);f->addRow(tr("Output folder:"),orr);m_epochs=new QSpinBox(this);m_epochs->setRange(1,10000);m_epochs->setValue(20);f->addRow(tr("Epochs:"),m_epochs);m_batch=new QSpinBox(this);m_batch->setRange(1,4096);m_batch->setValue(32);f->addRow(tr("Batch size:"),m_batch);m_lr=new QDoubleSpinBox(this);m_lr->setDecimals(6);m_lr->setRange(1e-7,10);m_lr->setValue(1e-3);f->addRow(tr("Learning rate:"),m_lr);m_val=new QDoubleSpinBox(this);m_val->setRange(0,0.9);m_val->setSingleStep(0.05);m_val->setValue(0.2);f->addRow(tr("Validation split:"),m_val);m_device=new QComboBox(this);m_device->addItems({"cuda","cpu"});f->addRow(tr("Device:"),m_device);l->insertLayout(1,f);m_progress=new QProgressBar(this);l->insertWidget(2,m_progress);m_metrics=new QTableWidget(0,5,this);m_metrics->setHorizontalHeaderLabels({"Epoch","Train loss","Validation loss","Accuracy","Progress"});m_metrics->horizontalHeader()->setStretchLastSection(true);l->insertWidget(3,m_metrics);auto*r=new QHBoxLayout;m_train=new QPushButton(tr("Start training"),this);auto*stopb=new QPushButton(tr("Stop"),this);r->addWidget(m_train);r->addWidget(stopb);l->insertLayout(4,r);connect(db,&QPushButton::clicked,this,&TrainingPage::chooseDataset);connect(ob,&QPushButton::clicked,this,&TrainingPage::chooseOutput);connect(m_train,&QPushButton::clicked,this,&TrainingPage::train);connect(stopb,&QPushButton::clicked,this,&TrainingPage::stop);if(c){connect(c->aiWorker(),&QtAiWorkerAdapter::metric,this,&TrainingPage::onMetric);connect(c->aiWorker(),&QtAiWorkerAdapter::modelReady,this,[this](const QString&p){const QString w=QFileInfo(QDir(p).filePath("weights.pt")).exists()?QStringLiteral("weights.pt"):QStringLiteral("weights.json");writeModelManifest(p.toStdString(),m_spec,m_dataset->text().toStdString(),w.toStdString());m_train->setEnabled(true);m_status->setText(tr("Model saved: %1").arg(p));});connect(c->aiWorker(),&QtAiWorkerAdapter::errorOccurred,this,[this](const QString&e){m_train->setEnabled(true);m_status->setText(tr("Error: %1").arg(e));});connect(c->aiWorker(),&QtAiWorker::finished,this,[this](bool){m_train->setEnabled(true);});}}
void TrainingPage::chooseDataset(){auto d=QFileDialog::getExistingDirectory(this,tr("Select dataset"),m_dataset->text());if(d.isEmpty())return;std::string err;if(!loadDatasetManifest(d.toStdString(),m_spec,nullptr,nullptr,&err)){QMessageBox::warning(this,tr("Dataset"),QString::fromStdString(err));return;}m_dataset->setText(d);if(m_context)m_context->rememberDataset(d);m_status->setText(tr("Loaded: %1 (%2 channels, %3 samples)").arg(QString::fromStdString(m_spec.displayName)).arg(m_spec.channels.size()).arg(m_spec.windowSamples));}
void TrainingPage::chooseOutput(){auto d=QFileDialog::getExistingDirectory(this,tr("Select output folder"),m_output->text());if(!d.isEmpty())m_output->setText(d);}
void TrainingPage::train(){if(!m_context||m_dataset->text().isEmpty()){m_status->setText(tr("Select a dataset first"));return;}if(m_spec.windowSamples==0&&!loadDatasetManifest(m_dataset->text().toStdString(),m_spec)){m_status->setText(tr("Invalid dataset"));return;}auto out=QString::fromStdString(nextModelBundlePath(m_output->text().toStdString()));m_metrics->setRowCount(0);m_progress->setValue(0);m_train->setEnabled(false);m_context->aiWorker()->startTraining(m_dataset->text(),out,QString::fromStdString(m_spec.modelFamily),m_epochs->value(),m_batch->value(),m_lr->value(),m_val->value(),1,m_device->currentText());m_status->setText(tr("Training started"));}
void TrainingPage::stop(){if(m_context)m_context->aiWorker()->stop();m_train->setEnabled(true);m_status->setText(tr("Training stopped"));}
void TrainingPage::onMetric(int epoch,int epochs,double tl,double vl,double acc){m_progress->setValue(int(100.0*epoch/std::max(1,epochs)));int r=m_metrics->rowCount();m_metrics->insertRow(r);for(int c=0;c<5;++c)m_metrics->setItem(r,c,new QTableWidgetItem(c==0?QString::number(epoch):c==1?QString::number(tl,'f',5):c==2?QString::number(vl,'f',5):c==3?QString::number(acc,'f',3):QString::number(100.0*epoch/std::max(1,epochs),'f',1)+"%"));}

InferencePage::InferencePage(AppContext*c,QWidget*p):AiPageBase(c,"Inference",p){auto*l=qobject_cast<QVBoxLayout*>(layout());auto*r=new QHBoxLayout;m_model=new QLineEdit(this);auto*b=new QPushButton(tr("Browse..."),this);r->addWidget(new QLabel(tr("Model (.aimodel):"),this));r->addWidget(m_model);r->addWidget(b);l->insertLayout(1,r);m_descriptor=new QLabel(tr("No model selected"),this);l->insertWidget(2,m_descriptor);m_top5=new QTableWidget(0,2,this);m_top5->setHorizontalHeaderLabels({"Label","Probability"});m_top5->horizontalHeader()->setStretchLastSection(true);l->insertWidget(3,m_top5);auto*br=new QHBoxLayout;m_start=new QPushButton(tr("Start inference"),this);m_stop=new QPushButton(tr("Stop"),this);br->addWidget(m_start);br->addWidget(m_stop);l->insertLayout(4,br);connect(b,&QPushButton::clicked,this,&InferencePage::chooseModel);connect(m_start,&QPushButton::clicked,this,&InferencePage::start);connect(m_stop,&QPushButton::clicked,this,&InferencePage::stop);if(c)connect(c->aiWorker(),&QtAiWorkerAdapter::inferenceResult,this,[this](const QVariantList&v){m_top5->setRowCount(0);for(const auto&x:v){auto o=x.toMap();int r=m_top5->rowCount();m_top5->insertRow(r);m_top5->setItem(r,0,new QTableWidgetItem(o.value("label").toString()));m_top5->setItem(r,1,new QTableWidgetItem(QString::number(o.value("probability").toDouble(),'f',4)));}});}
void InferencePage::chooseModel(){auto d=QFileDialog::getExistingDirectory(this,tr("Select model bundle"),m_model->text());if(d.isEmpty())return;ModelBundleInfo info;std::string err;if(!loadModelManifest(d.toStdString(),info,m_spec,&err)){QMessageBox::warning(this,tr("Model"),QString::fromStdString(err));return;}m_model->setText(d);if(m_context)m_context->rememberModel(d);m_descriptor->setText(tr("Task: %1  |  Family: %2  |  Window: %3 samples").arg(QString::fromStdString(info.taskId)).arg(QString::fromStdString(info.modelFamily)).arg(m_spec.windowSamples));}
void InferencePage::start(){if(!m_context||m_model->text().isEmpty()){m_status->setText(tr("Select a model first"));return;}ModelBundleInfo info;std::string err;if(!loadModelManifest(m_model->text().toStdString(),info,m_spec,&err)){m_status->setText(QString::fromStdString(err));return;}if(!m_context->acquireMode(AppContext::AcquisitionMode::Inference)){m_status->setText(tr("Device is busy with another mode"));return;}if(m_context->session())m_context->session()->stopStream();m_seen=0;m_window.clear();m_token=m_context->dataHub()->subscribe([this](const DataHub::Block&b){onBlock(b);});m_context->aiWorker()->startInference(m_model->text(),QString::fromStdString(info.modelFamily));if(m_context->session()){QVector<quint8> channels;for(auto c:m_spec.channels)channels.push_back(c);if(channels.isEmpty())for(int c=0;c<8;++c)channels.push_back(quint8(c));m_context->session()->startStream(channels);}m_status->setText(tr("Inference started"));}
void InferencePage::stop(){if(m_context){if(m_token)m_context->dataHub()->unsubscribe(m_token);m_token=0;m_context->aiWorker()->stop();if(m_context->session())m_context->session()->stopStream();m_context->releaseMode(AppContext::AcquisitionMode::Inference);}m_status->setText(tr("Inference stopped"));}
void InferencePage::onBlock(const DataHub::Block&b){if(b.samples.empty()||m_spec.windowSamples==0)return;for(size_t i=0;i<b.samples[0].size();++i){for(size_t c=0;c<b.samples.size();++c)m_window.push_back({float(b.samples[c][i])});++m_seen;if(m_seen>=m_spec.windowSamples){QVector<float>flat;for(const auto&v:m_window)flat+=v;const int rows=int(m_spec.windowSamples),channels=int(b.samples.size());QMetaObject::invokeMethod(this,[this,flat,rows,channels]{if(m_context)m_context->aiWorker()->sendWindow(flat,rows,channels);},Qt::QueuedConnection);m_window.clear();m_seen=0;}}}
