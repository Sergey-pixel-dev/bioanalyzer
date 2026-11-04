#include "notifications/Notifications.h"

#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QWidget>

Notifications::Notifications(QObject *parent) : QObject(parent) {}
Notifications::~Notifications() = default;

QString Notifications::scopeOf(QWidget *ctx)
{
    if (!ctx)
        return QString();
    const QString name = ctx->objectName();
    return name.isEmpty() ? QString::fromUtf8(ctx->metaObject()->className()) : name;
}

void Notifications::info(QWidget *ctx, const QString &title, const QString &text)
{
    showBox(ctx, QMessageBox::Information, title, text);
    emit messageEmitted(Level::Info, title, text);
}

void Notifications::warning(QWidget *ctx, const QString &title, const QString &text)
{
    showBox(ctx, QMessageBox::Warning, title, text);
    emit messageEmitted(Level::Warning, title, text);
}

void Notifications::error(QWidget *ctx, const QString &title, const QString &text)
{
    showBox(ctx, QMessageBox::Critical, title, text);
    emit messageEmitted(Level::Error, title, text);
}

void Notifications::showBox(QWidget *ctx, QMessageBox::Icon icon,
                            const QString &title, const QString &text)
{
    // Одинаковое сообщение, ещё не закрытое оператором, не повторяется:
    // отказ, приходящий много раз подряд, даёт одно окно.
    const QString key = title + QChar(0x1f) + text;
    if (QPointer<QMessageBox> open = m_openBoxes.value(key))
    {
        open->raise();
        open->activateWindow();
        return;
    }
    // Окно открывается над главным окном, даже если породившая сообщение
    // страница скрыта, и без вложенного цикла событий: асинхронный отказ
    // приходит из обработчика сигнала, который не должен ждать оператора.
    QWidget *parent = ctx ? ctx->window() : QApplication::activeWindow();
    auto *box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    m_openBoxes.insert(key, box);
    connect(box, &QObject::destroyed, this, [this, key]
            { m_openBoxes.remove(key); });
    box->open();
}

bool Notifications::question(QWidget *ctx, const QString &title, const QString &text)
{
    const auto answer = QMessageBox::question(ctx, title, text,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    emit messageEmitted(Level::Question, title, text);
    return answer == QMessageBox::Yes;
}

void Notifications::status(const QString &scope, const QString &text, Level level)
{
    if (scope.isEmpty())
        return;
    m_lastStatus.insert(scope, text);
    if (auto label = m_labels.value(scope))
        label->setText(text);
    emit statusChanged(scope, text, level);
}

void Notifications::clearStatus(const QString &scope)
{
    m_lastStatus.remove(scope);
    if (auto label = m_labels.value(scope))
        label->clear();
    emit statusChanged(scope, QString(), Level::Info);
}

QString Notifications::currentStatus(const QString &scope) const
{
    return m_lastStatus.value(scope);
}

void Notifications::attachStatusLabel(const QString &scope, QLabel *label)
{
    if (!label || scope.isEmpty())
        return;
    m_labels.insert(scope, label);
    if (m_lastStatus.contains(scope))
        label->setText(m_lastStatus.value(scope));
}

void Notifications::detachStatusLabel(const QString &scope)
{
    m_labels.remove(scope);
}
