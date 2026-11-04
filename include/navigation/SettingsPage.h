#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

class AppContext;
class QPushButton;
class QComboBox;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(AppContext *context, QWidget *parent = nullptr);

    // Language code saved in the settings ("en" or "ru"); read at start-up.
    static QString savedLanguage();

private slots:
    void onThemeToggled(bool lightTheme);
    void onLanguageChosen(int index);

private:
    AppContext *m_context = nullptr;
    QPushButton *m_themeToggle = nullptr;
    QComboBox *m_languageCombo = nullptr;
};

#endif
