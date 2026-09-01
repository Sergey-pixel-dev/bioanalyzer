#ifndef QTAIWORKERADAPTER_H
#define QTAIWORKERADAPTER_H
#include <QObject>
#include <QString>
#include <QProcess>
#include <QVariantList>
#include <QVector>
class QtAiWorkerAdapter : public QObject
{
 Q_OBJECT
public: explicit QtAiWorkerAdapter(QObject* p=nullptr);
 bool isRunning() const{return m_process.state()!=QProcess::NotRunning;}
public slots: void startTraining(const QString &dataset,const QString &outModel,const QString &family=QStringLiteral("custom"),int epochs=20,int batchSize=32,double learningRate=1e-3,double validationSplit=0.2,uint seed=1,const QString &device=QStringLiteral("cuda")); void startInference(const QString &model,const QString &family=QStringLiteral("custom")); void sendWindow(const QVector<float>& values,int rows,int channels); void stop();
signals: void started(); void progress(double value,const QString &message); void metric(int epoch,int epochs,double trainLoss,double valLoss,double accuracy); void inferenceResult(const QVariantList &top5); void modelReady(const QString &path); void errorOccurred(const QString &message); void finished(bool ok);
private slots: void readStdout(); void processError(QProcess::ProcessError); void processFinished(int,QProcess::ExitStatus);
private: void launch(const QStringList &args); void send(const QByteArray &line); QProcess m_process; QByteArray m_buffer; QString m_program;
};
using QtAiWorker = QtAiWorkerAdapter;
#endif
