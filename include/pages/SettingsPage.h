#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

class AppContext;
class QPushButton;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(AppContext *context, QWidget *parent = nullptr);

private slots:
    void onThemeToggled(bool lightTheme);

private:
    AppContext *m_context = nullptr;
    QPushButton *m_themeToggle = nullptr;
};

#endif
