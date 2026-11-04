#ifndef NOTIFICATIONS_NOTIFICATIONS_H
#define NOTIFICATIONS_NOTIFICATIONS_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QPointer>
#include <QMessageBox>

class QWidget;
class QLabel;

// Централизованный модуль сообщений интерфейса.
//
// Собирает в одном месте два вида взаимодействия с пользователем:
//   1) модальные диалоги (эквивалент QMessageBox);
//   2) «персистентный статус» страницы — короткое сообщение о текущем
//      состоянии, отображаемое, как правило, в QLabel-е внизу страницы.
//
// Логическое деление статусов происходит по scope-ам: строковых
// идентификаторов страницы или подсистемы («device», «monitoring»,
// «data_collection» и т. п.). Страница один раз привязывает свою метку
// к своему scope-у через attachStatusLabel(), после чего все обновления
// статуса из любого места приложения корректно попадают в её метку.
//
// Модуль намеренно не хранит истории — только последнее значение
// статуса, — так как этого достаточно для нижней строки страницы.
class Notifications : public QObject
{
    Q_OBJECT
public:
    enum class Level { Info, Warning, Error, Question };

    explicit Notifications(QObject *parent = nullptr);
    ~Notifications() override;

    // Окна сообщений. Ошибки и отказы выводятся только окном: строка
    // состояния страницы показывает лишь ход работы (status()). Окно
    // открывается без ожидания ответа, над главным окном; одинаковое
    // незакрытое сообщение не повторяется.
    void info    (QWidget *ctx, const QString &title, const QString &text);
    void warning (QWidget *ctx, const QString &title, const QString &text);
    void error   (QWidget *ctx, const QString &title, const QString &text);

    // Возвращает true, если пользователь нажал Yes.
    bool question(QWidget *ctx, const QString &title, const QString &text);

    // Персистентный статус по scope-у. Обновляет привязанную метку,
    // если она есть, и посылает сигнал statusChanged.
    void status(const QString &scope, const QString &text, Level level = Level::Info);
    void clearStatus(const QString &scope);
    QString currentStatus(const QString &scope) const;

    // Привязка QLabel к scope-у. После привязки любой status(scope, ...)
    // будет обновлять текст метки автоматически. Метка удерживается
    // через QPointer — если она уничтожается, привязка молча пропадает.
    void attachStatusLabel(const QString &scope, QLabel *label);
    void detachStatusLabel(const QString &scope);

signals:
    void statusChanged(const QString &scope, const QString &text, Level level);
    void messageEmitted(Level level, const QString &title, const QString &text);

private:
    static QString scopeOf(QWidget *ctx);
    void showBox(QWidget *ctx, QMessageBox::Icon icon, const QString &title, const QString &text);

    QHash<QString, QString> m_lastStatus;
    QHash<QString, QPointer<QLabel>> m_labels;
    QHash<QString, QPointer<QMessageBox>> m_openBoxes; // открытые окна по заголовку и тексту
};

#endif // NOTIFICATIONS_NOTIFICATIONS_H
