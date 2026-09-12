#include "notifications/Notifications.h"

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
    QMessageBox::information(ctx, title, text);
    emit messageEmitted(Level::Info, title, text);
    status(scopeOf(ctx), text, Level::Info);
}

void Notifications::warning(QWidget *ctx, const QString &title, const QString &text)
{
    QMessageBox::warning(ctx, title, text);
    emit messageEmitted(Level::Warning, title, text);
    status(scopeOf(ctx), text, Level::Warning);
}

void Notifications::error(QWidget *ctx, const QString &title, const QString &text)
{
    QMessageBox::critical(ctx, title, text);
    emit messageEmitted(Level::Error, title, text);
    status(scopeOf(ctx), text, Level::Error);
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
