#ifndef AI_AIPAGEBASE_H
#define AI_AIPAGEBASE_H

#include <QWidget>

class AppContext;
class QLabel;

// Базовый класс страниц раздела AI (сбор данных, обучение, инференс).
// Общая инфраструктура: заголовок вида «AI / <название>» и статусная
// строка внизу, привязываемая к модулю сообщений через объектное имя
// страницы (задаётся конкретным наследником).
class AiPageBase : public QWidget
{
public:
    explicit AiPageBase(AppContext *context, const QString &title, QWidget *parent = nullptr);

protected:
    AppContext *m_context;
    QLabel *m_status;
};

#endif // AI_AIPAGEBASE_H
