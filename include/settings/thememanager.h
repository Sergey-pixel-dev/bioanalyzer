#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>

class QApplication;
class QSettings;

enum class AppTheme
{
    Dark,
    Light
};

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    explicit ThemeManager(QApplication *application, QObject *parent = nullptr);

    AppTheme theme() const noexcept { return m_theme; }
    bool isDark() const noexcept { return m_theme == AppTheme::Dark; }

    void setTheme(AppTheme theme);
    void restore(QSettings &settings);
    void save(QSettings &settings) const;

signals:
    void themeChanged(AppTheme theme);

private:
    void apply();

    QApplication *m_application = nullptr;
    AppTheme m_theme = AppTheme::Dark;
};

#endif
