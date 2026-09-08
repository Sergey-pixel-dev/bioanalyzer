#include "app/thememanager.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

ThemeManager::ThemeManager(QApplication *application, QObject *parent)
    : QObject(parent), m_application(application)
{
    apply();
}

void ThemeManager::setTheme(AppTheme theme)
{
    if (m_theme == theme)
        return;
    m_theme = theme;
    apply();
    QSettings settings;
    save(settings);
    emit themeChanged(m_theme);
}

void ThemeManager::restore(QSettings &settings)
{
    const QString value = settings.value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString();
    m_theme = value.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0
                  ? AppTheme::Light
                  : AppTheme::Dark;
    apply();
}

void ThemeManager::save(QSettings &settings) const
{
    settings.setValue(QStringLiteral("appearance/theme"),
                      m_theme == AppTheme::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

void ThemeManager::apply()
{
    if (!m_application)
        return;
    m_application->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const bool dark = m_theme == AppTheme::Dark;
    QPalette p = dark ? QPalette(QColor(30, 33, 39)) : QPalette(QColor(245, 246, 248));
    const QColor window = dark ? QColor(30, 33, 39) : QColor(245, 246, 248);
    const QColor base = dark ? QColor(24, 26, 31) : QColor(255, 255, 255);
    const QColor surface = dark ? QColor(38, 42, 50) : QColor(232, 235, 240);
    const QColor text = dark ? QColor(224, 226, 230) : QColor(35, 38, 44);
    const QColor subtle = dark ? QColor(150, 155, 165) : QColor(105, 110, 120);
    const QColor accent = dark ? QColor(0, 150, 200) : QColor(0, 108, 170);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, surface);
    p.setColor(QPalette::ToolTipBase, surface);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, subtle);
    p.setColor(QPalette::Disabled, QPalette::WindowText, subtle);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, subtle);
    m_application->setPalette(p);

    const QString bg = dark ? QStringLiteral("#181a1f") : QStringLiteral("#ffffff");
    const QString panel = dark ? QStringLiteral("#2f343d") : QStringLiteral("#e8ebf0");
    const QString border = dark ? QStringLiteral("#3a3f49") : QStringLiteral("#c7ccd5");
    const QString selected = dark ? QStringLiteral("#0096c8") : QStringLiteral("#006cae");
    m_application->setStyleSheet(QStringLiteral(
        "QWidget { font-size: 10.5pt; }"
        "QGroupBox { border: 1px solid %1; border-radius: 6px; margin-top: 10px; padding: 6px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QPushButton { background: %2; border: 1px solid %1; border-radius: 5px; padding: 5px 12px; }"
        "QPushButton:hover { background: %3; } QPushButton:checked { background: %4; color: white; }"
        "QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { background: %5; border: 1px solid %1; border-radius: 5px; padding: 4px 7px; }"
        "QTableWidget, QListWidget { background: %5; border: 1px solid %1; }"
        "QHeaderView::section { background: %2; border: none; padding: 5px; }"
        "QTabBar::tab { background: %2; border: 1px solid %1; padding: 5px 12px; }"
        "QTabBar::tab:selected { background: %4; color: white; }"
        "QSlider::groove:horizontal { height: 6px; background: %2; }"
        "QSlider::handle:horizontal { background: %4; width: 14px; margin: -5px 0; border-radius: 7px; }")
        .arg(border, panel, dark ? QStringLiteral("#3a404d") : QStringLiteral("#dce1e8"), selected, bg));
}
