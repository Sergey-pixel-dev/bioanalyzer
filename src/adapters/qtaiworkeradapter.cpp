#include "adapters/qtaiworkeradapter.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
QtAiWorkerAdapter::QtAiWorkerAdapter(QObject*p):QObject(p){connect(&m_process,&QProcess::readyReadStandardOutput,this,&QtAiWorkerAdapter::readStdout);connect(&m_process,&QProcess::errorOccurred,this,&QtAiWorkerAdapter::processError);connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&QtAiWorkerAdapter::processFinished);m_program=QDir(QCoreApplication::applicationDirPath()).filePath(".venv/bin/python");if(!QFileInfo::exists(m_program))m_program=QStringLiteral("python3");}
void QtAiWorkerAdapter::launch(const QStringList&a){if(isRunning()){emit errorOccurred("AI worker is already running");return;}QString worker=QDir(QCoreApplication::applicationDirPath()).filePath("ai_worker/worker.py");if(!QFileInfo::exists(worker))worker=QStringLiteral("ai_worker/worker.py");m_process.start(m_program,QStringList()<<worker<<a);if(!m_process.waitForStarted(2000))emit errorOccurred("Unable to start AI worker");else emit started();}
void QtAiWorkerAdapter::send(const QByteArray&l){if(isRunning()){m_process.write(l);m_process.write("\n");}}
void QtAiWorkerAdapter::startTraining(const QString&d,const QString&o,const QString&f,int epochs,int batch,double lr,double val,uint seed,const QString&device){launch({"--mode","train","--dataset",d,"--output",o,"--family",f,"--epochs",QString::number(epochs),"--batch-size",QString::number(batch),"--learning-rate",QString::number(lr,'g',12),"--validation-split",QString::number(val,'g',12),"--seed",QString::number(seed),"--device",device});}
void QtAiWorkerAdapter::startInference(const QString&m,const QString&f){launch({"--mode","infer","--model",m,"--family",f});}
void QtAiWorkerAdapter::sendWindow(const QVector<float>&v,int rows,int ch){QJsonObject o;o["cmd"]="window";o["rows"]=rows;o["channels"]=ch;QJsonArray a;for(float x:v)a.append(double(x));o["values"]=a;send(QJsonDocument(o).toJson(QJsonDocument::Compact));}
void QtAiWorkerAdapter::stop(){if(isRunning()){send("{\"cmd\":\"stop\"}");m_process.terminate();}}
void QtAiWorkerAdapter::readStdout(){m_buffer+=m_process.readAllStandardOutput();while(true){int i=m_buffer.indexOf('\n');if(i<0)break;QByteArray l=m_buffer.left(i).trimmed();m_buffer.remove(0,i+1);QJsonParseError e;auto d=QJsonDocument::fromJson(l,&e);if(e.error!=QJsonParseError::NoError||!d.isObject())continue;auto o=d.object();QString type=o["type"].toString();if(type=="progress")emit progress(o["value"].toDouble(),o["message"].toString());else if(type=="metric")emit metric(o["epoch"].toInt(),o["epochs"].toInt(),o["train_loss"].toDouble(),o["val_loss"].toDouble(),o["accuracy"].toDouble());else if(type=="result"){if(o["kind"].toString()=="model")emit modelReady(o["path"].toString());else emit inferenceResult(o["top5"].toArray().toVariantList());}else if(type=="error")emit errorOccurred(o["message"].toString());}}
void QtAiWorkerAdapter::processError(QProcess::ProcessError){emit errorOccurred("Unable to start AI worker");}
void QtAiWorkerAdapter::processFinished(int,QProcess::ExitStatus s){emit finished(s==QProcess::NormalExit);}
