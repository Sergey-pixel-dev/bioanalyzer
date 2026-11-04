#ifndef AI_AIPAGEBASE_H
#define AI_AIPAGEBASE_H

#include "notifications/Notifications.h"

#include <QWidget>

class AppContext;
class QLabel;

// Базовый класс страниц раздела AI (сбор данных, обучение, инференс).
// Общая инфраструктура: заголовок вида «AI / <название>» и строка
// состояния под ним, привязываемая к модулю сообщений через объектное имя
// страницы (задаётся конкретным наследником в setObjectName). Строка
// показывает ход работы; ошибки и отказы выводятся окном (fail()).
//
// setStatus() маршрутизирует сообщение через Notifications: сервис
// обновляет привязанную метку и рассылает сигнал statusChanged(scope, ...),
// на который может подписаться, например, статус-бар главного окна.
class AiPageBase : public QWidget
{
    Q_OBJECT
public:
    explicit AiPageBase(AppContext *context, const QString &title, QWidget *parent = nullptr);

protected:
    // Ход работы — в строку состояния страницы.
    void setStatus(const QString &text, Notifications::Level level = Notifications::Level::Info);
    // Ошибка или отказ — окном службы сообщений с заголовком страницы.
    void fail(const QString &text);

    AppContext *m_context;
    QLabel *m_status;
    QString m_title;
};

#endif // AI_AIPAGEBASE_H
