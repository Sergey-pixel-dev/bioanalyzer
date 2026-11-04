#ifndef QTAIWORKERADAPTER_H
#define QTAIWORKERADAPTER_H
#include <QObject>
#include <QString>
#include <QProcess>
#include <QQueue>
#include <QVector>
#include <QJsonObject>

#include <string>
#include <utility>
#include <vector>

// Answer of a classification model on one window: labels with their
// probabilities, the most probable first.
using ClassificationResult = std::vector<std::pair<std::string, double>>;

// Граничный класс: обмен с рабочим процессом Python строками JSON. Сверяет
// версию протокола и превращает сообщения процесса в сигналы с готовыми
// значениями: ход обучения --- в значения величин по эпохам, ответ модели ---
// в результат её запуска (сейчас только результат классификации). Свой
// экземпляр есть у сеанса обучения и у сеанса распознавания.
class QtAiWorkerAdapter : public QObject
{
    Q_OBJECT
public:
    explicit QtAiWorkerAdapter(QObject *p = nullptr);
    // Ends a running process without reporting it: its owner is gone.
    ~QtAiWorkerAdapter() override;
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }
public slots:
    void startInference(const QString &model, const QString &family);
    void sendWindow(const QVector<float> &values, int rows, int channels);
    void stop();
    void describeFamily(const QString &family);
    void startTrainingConfig(const QString &dataset, const QString &outModel, const QString &family, const QJsonObject &config);
signals:
    void started();
    void progress(double value, const QString &message);
    // Values the model reports after an epoch, named as in its description
    // (outputs.training); `best` --- the weights of this epoch are kept.
    void metric(int epoch, int epochs, const QJsonObject &values, bool best);
    void classificationResult(const ClassificationResult &result);
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
    bool m_stopping = false; // stop() asked the process to end
    // A message of another protocol version has been reported for this run.
    bool m_protocolReported = false;
    void launch(const QStringList &args);
    void send(const QByteArray &line);
    void flushPendingWindows();
    void clearPendingWindows();
    QProcess m_process;
    QByteArray m_buffer;
    QByteArray m_stderr;
    QString m_program;
    QStringList m_programArgs; // arguments of the interpreter before the script ("-3" of py)
    QQueue<QByteArray> m_pendingWindows;
};
using QtAiWorker = QtAiWorkerAdapter;
#endif
