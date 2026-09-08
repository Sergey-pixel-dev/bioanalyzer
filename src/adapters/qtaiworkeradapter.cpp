#include "adapters/qtaiworkeradapter.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
QtAiWorkerAdapter::QtAiWorkerAdapter(QObject *p) : QObject(p)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &QtAiWorkerAdapter::readStdout);
    connect(&m_process, &QProcess::readyReadStandardError, this, &QtAiWorkerAdapter::readStderr);
    connect(&m_process, &QProcess::errorOccurred, this, &QtAiWorkerAdapter::processError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &QtAiWorkerAdapter::processFinished);
    m_program = QDir(QCoreApplication::applicationDirPath()).filePath(".venv/bin/python");
    if (!QFileInfo::exists(m_program))
        m_program = QStringLiteral("python3");
}
void QtAiWorkerAdapter::launch(const QStringList &a)
{
    if (isRunning())
    {
        emit errorOccurred("AI worker is already running");
        return;
    }
    QString worker = QDir(QCoreApplication::applicationDirPath()).filePath("ai_worker/worker.py");
    if (!QFileInfo::exists(worker))
        worker = QStringLiteral("ai_worker/worker.py");
    m_buffer.clear();
    m_stderr.clear();
    clearPendingWindows();
    m_process.start(m_program, QStringList() << worker << a);
    if (!m_process.waitForStarted(2000))
        emit errorOccurred("Unable to start AI worker");
    else
    {
        flushPendingWindows();
        emit started();
    }
}
void QtAiWorkerAdapter::send(const QByteArray &l)
{
    if (isRunning())
    {
        m_process.write(l);
        m_process.write("\n");
    }
}
void QtAiWorkerAdapter::startTrainingConfig(const QString &d, const QString &o, const QString &f, const QJsonObject &config) { launch({"--mode", "train", "--dataset", d, "--output", o, "--family", f, "--config", QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)), "--models-dir", QDir(QCoreApplication::applicationDirPath()).filePath("ai_worker")}); }
void QtAiWorkerAdapter::describeFamily(const QString &f) { launch({"--mode", "describe", "--family", f, "--models-dir", QDir(QCoreApplication::applicationDirPath()).filePath("ai_worker")}); }
void QtAiWorkerAdapter::startInference(const QString &m, const QString &f) { launch({"--mode", "infer", "--model", m, "--family", f, "--models-dir", QDir(QCoreApplication::applicationDirPath()).filePath("ai_worker")}); }
void QtAiWorkerAdapter::sendWindow(const QVector<float> &v, int rows, int ch)
{
    QJsonObject o;
    o["cmd"] = "window";
    o["rows"] = rows;
    o["channels"] = ch;
    QJsonArray a;
    for (float x : v)
        a.append(double(x));
    o["values"] = a;
    m_pendingWindows.enqueue(QJsonDocument(o).toJson(QJsonDocument::Compact));
    flushPendingWindows();
}
void QtAiWorkerAdapter::flushPendingWindows()
{
    if (!isRunning())
        return;
    while (!m_pendingWindows.isEmpty())
    {
        send(m_pendingWindows.dequeue());
    }
}
void QtAiWorkerAdapter::clearPendingWindows()
{
    m_pendingWindows.clear();
}
void QtAiWorkerAdapter::stop()
{
    if (isRunning())
    {
        clearPendingWindows();
        send("{\"cmd\":\"stop\"}");
        m_process.terminate();
    }
    else
        clearPendingWindows();
}
void QtAiWorkerAdapter::readStdout()
{
    m_buffer += m_process.readAllStandardOutput();
    while (true)
    {
        int i = m_buffer.indexOf('\n');
        if (i < 0)
            break;
        QByteArray l = m_buffer.left(i).trimmed();
        m_buffer.remove(0, i + 1);
        QJsonParseError e;
        auto d = QJsonDocument::fromJson(l, &e);
        if (e.error != QJsonParseError::NoError || !d.isObject())
            continue;
        auto o = d.object();
        QString type = o["type"].toString();
        if (type == "progress")
            emit progress(o["value"].toDouble(), o["message"].toString());
        else if (type == "metric")
            emit metric(o["epoch"].toInt(), o["epochs"].toInt(), o["train_loss"].toDouble(), o["val_loss"].toDouble(), o["accuracy"].toDouble());
        else if (type == "meta")
            emit familyMeta(o["meta"].toObject());
        else if (type == "result")
        {
            auto out = o["output"].toObject();
            if (out.value("kind").toString() == "model")
                emit modelReady(out.value("path").toString());
            else
            {
                emit inferenceOutput(out);
                if (out.value("kind").toString() == "top_k")
                    emit inferenceResult(out.value("items").toArray().toVariantList());
            }
        }
        else if (type == "error")
            emit errorOccurred(o["message"].toString());
    }
}
void QtAiWorkerAdapter::readStderr()
{
    m_stderr += m_process.readAllStandardError();
}
void QtAiWorkerAdapter::processError(QProcess::ProcessError)
{
    clearPendingWindows();
    emit errorOccurred("Unable to start AI worker");
}
void QtAiWorkerAdapter::processFinished(int exitCode, QProcess::ExitStatus s)
{
    clearPendingWindows();
    const bool ok = s == QProcess::NormalExit && exitCode == 0;
    if (!ok && !m_stderr.trimmed().isEmpty())
        emit errorOccurred(QStringLiteral("AI worker: %1").arg(QString::fromLocal8Bit(m_stderr.trimmed())));
    emit finished(ok);
}
