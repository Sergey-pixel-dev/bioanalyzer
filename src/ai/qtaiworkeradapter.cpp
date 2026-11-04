#include "ai/qtaiworkeradapter.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace
{
    // Version of the worker protocol (PROTOCOL in ai_worker/worker.py).
    constexpr int kWorkerProtocol = 3;
}

QtAiWorkerAdapter::QtAiWorkerAdapter(QObject *p) : QObject(p)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &QtAiWorkerAdapter::readStdout);
    connect(&m_process, &QProcess::readyReadStandardError, this, &QtAiWorkerAdapter::readStderr);
    connect(&m_process, &QProcess::errorOccurred, this, &QtAiWorkerAdapter::processError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &QtAiWorkerAdapter::processFinished);
    // Python с NumPy, SciPy и PyTorch в дистрибутив не входит, его ставит
    // пользователь: виртуальное окружение .venv в папке программы, иначе
    // интерпретатор из PATH. Под Windows установщик python.org по умолчанию
    // ставит только py, а python в PATH не добавляет.
    const QDir appDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    const QString venv = appDir.filePath(QStringLiteral(".venv/Scripts/python.exe"));
    if (QFileInfo::exists(venv))
        m_program = venv;
    else if (!QStandardPaths::findExecutable(QStringLiteral("py")).isEmpty())
    {
        m_program = QStringLiteral("py");
        m_programArgs = QStringList{QStringLiteral("-3")};
    }
    else
        m_program = QStringLiteral("python");
#else
    const QString venv = appDir.filePath(QStringLiteral(".venv/bin/python"));
    m_program = QFileInfo::exists(venv) ? venv : QStringLiteral("python3");
#endif
    // Сообщения процесса и его ошибки идут в UTF-8 и под Windows с другой
    // кодовой страницей консоли (программа читает их как UTF-8).
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    m_process.setProcessEnvironment(environment);
}
QtAiWorkerAdapter::~QtAiWorkerAdapter()
{
    // Сигналы процесса больше некому принимать: при закрытии программы во
    // время обучения процесс завершается молча.
    m_process.disconnect(this);
    if (isRunning())
    {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}
void QtAiWorkerAdapter::launch(const QStringList &a)
{
    if (isRunning())
    {
        emit errorOccurred(tr("The AI worker is already running"));
        return;
    }
    QString worker = QDir(QCoreApplication::applicationDirPath()).filePath("ai_worker/worker.py");
    if (!QFileInfo::exists(worker))
        worker = QStringLiteral("ai_worker/worker.py");
    m_buffer.clear();
    m_stderr.clear();
    m_stopping = false;
    m_protocolReported = false;
    clearPendingWindows();
    m_process.start(m_program, QStringList() << m_programArgs << worker << a);
    if (!m_process.waitForStarted(2000))
    {
        // An interpreter that is not found is reported by processError().
        if (m_process.error() != QProcess::FailedToStart)
            emit errorOccurred(tr("Unable to start the AI worker (Python)"));
    }
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
        // The process is ended on purpose: its exit is not a failure.
        m_stopping = true;
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
        // A process of another protocol version is not understood: one
        // message for the run, the rest is ignored.
        if (o.value("protocol").toInt() != kWorkerProtocol)
        {
            if (!m_protocolReported)
            {
                m_protocolReported = true;
                emit errorOccurred(tr("The protocol version of the AI worker differs from the one this program reads"));
            }
            continue;
        }
        QString type = o["type"].toString();
        if (type == "progress")
            emit progress(o["value"].toDouble(), o["message"].toString());
        else if (type == "metric")
            emit metric(o["epoch"].toInt(), o["epochs"].toInt(), o["values"].toObject(), o["best"].toBool());
        else if (type == "meta")
            emit familyMeta(o["meta"].toObject());
        else if (type == "result")
        {
            auto out = o["output"].toObject();
            const QString kind = out.value("kind").toString();
            if (kind == "model")
                emit modelReady(out.value("path").toString());
            else if (kind == "top_k")
            {
                // Результат классификации: метки с вероятностями.
                ClassificationResult result;
                for (const auto &v : out.value("items").toArray())
                {
                    const auto item = v.toObject();
                    result.emplace_back(item.value("label").toString().toStdString(),
                                        item.value("probability").toDouble());
                }
                emit classificationResult(result);
            }
            else
                emit errorOccurred(tr("The program cannot show results of the kind \"%1\"").arg(kind));
        }
        else if (type == "error")
            emit errorOccurred(o["message"].toString());
    }
}
void QtAiWorkerAdapter::readStderr()
{
    m_stderr += m_process.readAllStandardError();
}
void QtAiWorkerAdapter::processError(QProcess::ProcessError error)
{
    clearPendingWindows();
    // Only a process that did not start is reported as such; a crash is
    // reported by processFinished(), and a stop requested by the
    // application is not an error at all.
    if (error == QProcess::FailedToStart)
        emit errorOccurred(tr("Unable to start the AI worker: Python 3 not found. Training and recognition need Python 3 with NumPy, SciPy and PyTorch"));
}
void QtAiWorkerAdapter::processFinished(int exitCode, QProcess::ExitStatus s)
{
    clearPendingWindows();
    const bool stopped = m_stopping;
    m_stopping = false;
    const bool ok = stopped || (s == QProcess::NormalExit && exitCode == 0);
    if (!ok && !m_stderr.trimmed().isEmpty())
        emit errorOccurred(tr("AI worker: %1").arg(QString::fromLocal8Bit(m_stderr.trimmed())));
    emit finished(ok);
}
