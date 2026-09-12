#ifndef AI_AIPAGEBASE_H
#define AI_AIPAGEBASE_H

#include "notifications/Notifications.h"

#include <QWidget>

class AppContext;
class QLabel;

// Базовый класс страниц раздела AI (сбор данных, обучение, инференс).
// Общая инфраструктура: заголовок вида «AI / <название>» и статусная
// строка внизу, привязываемая к модулю сообщений через объектное имя
// страницы (задаётся конкретным наследником в setObjectName).
//
// setStatus() маршрутизирует сообщение через Notifications: сервис
// обновляет привязанную метку и рассылает сигнал statusChanged(scope, ...),
// на который может подписаться, например, статус-бар главного окна.
class AiPageBase : public QWidget
{
public:
    explicit AiPageBase(AppContext *context, const QString &title, QWidget *parent = nullptr);

protected:
    void setStatus(const QString &text, Notifications::Level level = Notifications::Level::Info);

    AppContext *m_context;
    QLabel *m_status;
};

#endif // AI_AIPAGEBASE_H
