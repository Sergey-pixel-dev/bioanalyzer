#ifndef QTAIWORKERADAPTER_H
#define QTAIWORKERADAPTER_H
#include <QObject>
#include <QString>
#include <QProcess>
#include <QQueue>
#include <QVariantList>
#include <QVector>
#include <QJsonObject>
class QtAiWorkerAdapter : public QObject
{
    Q_OBJECT
public:
    explicit QtAiWorkerAdapter(QObject *p = nullptr);
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }
public slots:
    void startInference(const QString &model, const QString &family = QStringLiteral("custom"));
    void sendWindow(const QVector<float> &values, int rows, int channels);
    void stop();
    void describeFamily(const QString &family);
    void startTrainingConfig(const QString &dataset, const QString &outModel, const QString &family, const QJsonObject &config);
signals:
    void started();
    void progress(double value, const QString &message);
    void metric(int epoch, int epochs, double trainLoss, double valLoss, double accuracy);
    void inferenceResult(const QVariantList &top5);
    void inferenceOutput(const QJsonObject &output);
    void familyMeta(const QJsonObject &meta);
    void modelReady(const QString &path);
    void errorOccurred(const QString &message);
    void finished(bool ok);
private slots:
    void readStdout();
    void readStderr();
    void processError(QProcess::ProcessError);
    void processFinished(int, QProcess::ExitStatus);

private:
    void launch(const QStringList &args);
    void send(const QByteArray &line);
    void flushPendingWindows();
    void clearPendingWindows();
    QProcess m_process;
    QByteArray m_buffer;
    QByteArray m_stderr;
    QString m_program;
    QQueue<QByteArray> m_pendingWindows;
};
using QtAiWorker = QtAiWorkerAdapter;
#endif
