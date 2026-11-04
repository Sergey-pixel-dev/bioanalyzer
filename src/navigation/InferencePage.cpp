#include "navigation/InferencePage.h"
#include "ai/inferencesession.h"
#include "navigation/AppContext.h"
#include "notifications/Notifications.h"
#include "ui_inferencepage.h"

#include <QAbstractItemView>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

InferencePage::InferencePage(AppContext *c, QWidget *p) : AiPageBase(c, tr("Inference"), p)
{
    setObjectName(QStringLiteral("inference"));
    if (c && c->notifications())
        c->notifications()->attachStatusLabel(objectName(), m_status);

    auto *l = qobject_cast<QVBoxLayout *>(layout());
    auto *content = new QWidget(this);
    Ui::InferencePageForm form;
    form.setupUi(content);
    m_model = form.modelPath;
    m_modelBrowse = form.browseButton;
    m_descriptor = form.descriptorLabel;
    m_inferenceStride = form.inferenceStride;
    m_smoothing = form.smoothingSeconds;
    m_result = form.resultLabel;
    m_top5 = form.resultsTable;
    m_start = form.startButton;
    m_stop = form.stopButton;
    m_top5->setHorizontalHeaderLabels({tr("Label"), tr("Probability")});
    m_top5->horizontalHeader()->setStretchLastSection(true);
    m_top5->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_model->setReadOnly(true);
    m_inferenceStride->setEnabled(false);
    m_stop->setEnabled(false);
    l->insertWidget(2, content, 1);

    connect(m_modelBrowse, &QPushButton::clicked, this, &InferencePage::chooseModel);
    connect(m_start, &QPushButton::clicked, this, &InferencePage::start);
    connect(m_stop, &QPushButton::clicked, this, &InferencePage::stop);
    connect(m_inferenceStride, qOverload<int>(&QSpinBox::valueChanged),
            this, &InferencePage::onStrideChanged);

    if (!c)
        return;

    // Сеанс распознавания получает поток и способ узнать текущий сеанс
    // устройства, а общего состояния приложения не знает.
    m_session = new InferenceSession(c->dataHub(), [c] { return c->session(); }, this);
    // Сглаживание касается только показа, поэтому меняется и во время
    // распознавания.
    m_session->setSmoothing(m_smoothing->value());
    connect(m_smoothing, qOverload<double>(&QDoubleSpinBox::valueChanged), m_session,
            &InferenceSession::setSmoothing);
    connect(m_session, &InferenceSession::started, this,
            [this](std::size_t window, std::size_t stride) {
                m_top5->setRowCount(0);
                m_result->setText(QStringLiteral("-"));
                applyRunningState(true);
                setStatus(tr("Inference started; waiting for a %1-sample window (stride %2)")
                              .arg(window)
                              .arg(stride));
            });
    connect(m_session, &InferenceSession::stopped, this, [this] {
        applyRunningState(false);
        setStatus(tr("Inference stopped"));
    });
    // Ошибка обработки блока повторялась бы на каждом следующем блоке:
    // распознавание останавливается, причина выводится одним окном.
    connect(m_session, &InferenceSession::failed, this,
            [this](const QString &reason)
            {
                if (!m_session->running())
                    return;
                m_session->stop();
                fail(tr("Recognition stopped: %1").arg(reason));
            });
    connect(m_session, &InferenceSession::progressed, this,
            [this](quint64 buffered, quint64 window, quint64 sent) {
                if (sent == 0)
                    setStatus(tr("Collecting inference window: %1 / %2 samples")
                                  .arg(buffered)
                                  .arg(window));
                else
                    setStatus(tr("Inference windows queued: %1; results received: %2")
                                  .arg(sent)
                                  .arg(m_session->resultsReceived()));
            });

    // Страница общается только с сеансом распознавания: процесс принадлежит
    // ему, а ответ модели сеанс сглаживает сам и сообщает, что показ обновлён.
    connect(m_session, &InferenceSession::workerReady, this,
            [this] { setStatus(tr("Inference worker ready; collecting data")); });
    connect(m_session, &InferenceSession::resultReceived, this, [this] {
        // Показываются сглаженные вероятности и жест с гистерезисом.
        const auto averaged = m_session->smoother().averaged();
        m_top5->setRowCount(static_cast<int>(averaged.size()));
        for (int row = 0; row < static_cast<int>(averaged.size()); ++row)
        {
            m_top5->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(averaged[row].first)));
            m_top5->setItem(row, 1, new QTableWidgetItem(QString::number(averaged[row].second, 'f', 4)));
        }
        m_result->setText(QString::fromStdString(m_session->smoother().shown()));
        setStatus(tr("Inference result received (%n window(s))", nullptr,
                     static_cast<int>(m_session->windowsSent())));
    });
}

void InferencePage::applyRunningState(bool running)
{
    m_start->setEnabled(!running);
    m_stop->setEnabled(running);
    m_modelBrowse->setEnabled(!running);
    m_inferenceStride->setEnabled(!running && m_session && m_session->modelLoaded());
}

void InferencePage::chooseModel()
{
    if (!m_session)
        return;
    const auto dir = QFileDialog::getExistingDirectory(this, tr("Select model bundle"),
                                                       m_model->text());
    if (dir.isEmpty())
        return;
    QString error;
    if (!m_session->loadModel(dir, &error))
    {
        fail(error);
        return;
    }
    const auto &spec = m_session->model().spec();
    const auto &info = m_session->model().info();
    m_model->setText(dir);
    if (m_inferenceStride && spec.windowSamples > 0)
    {
        // Шаг, предложенный моделью (четверть короткого окна), иначе окно
        // целиком, как у моделей, обученных на пробах.
        const std::size_t stride = spec.strideSamples > 0 && spec.strideSamples <= spec.windowSamples
                                       ? spec.strideSamples
                                       : spec.windowSamples;
        const QSignalBlocker blocker(m_inferenceStride);
        m_inferenceStride->setValue(static_cast<int>(std::min<std::size_t>(stride, 100000000)));
    }
    m_inferenceStride->setEnabled(true);
    m_model->setReadOnly(true);
    m_descriptor->setText(tr("Task: %1  |  Family: %2  |  Window: %3 samples")
                              .arg(QString::fromStdString(info.taskId))
                              .arg(QString::fromStdString(info.modelFamily))
                              .arg(spec.windowSamples));
}

void InferencePage::start()
{
    if (!m_session)
        return;
    QString error;
    if (!m_session->start(static_cast<std::size_t>(m_inferenceStride->value()), &error))
        fail(error);
}

void InferencePage::stop()
{
    if (m_session)
        m_session->stop();
}

void InferencePage::onStrideChanged(int samples)
{
    if (!m_session)
        return;
    if (m_session->running())
    {
        fail(tr("Stop inference before changing the stride"));
        return;
    }
    if (samples > 0 && m_session->modelLoaded())
        setStatus(tr("Inference stride set to %1 samples").arg(samples));
}
