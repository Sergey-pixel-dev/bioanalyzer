#include "pages/SettingsPage.h"

#include "app/AppContext.h"
#include "app/thememanager.h"
#include "ui_settingspage.h"
#include <QPushButton>
#include <QSignalBlocker>

SettingsPage::SettingsPage(AppContext *context, QWidget *parent)
    : QWidget(parent), m_context(context)
{
    Ui::SettingsPageForm ui;
    ui.setupUi(this);
    m_themeToggle = ui.themeToggle;
    connect(m_themeToggle, &QPushButton::toggled,
            this, &SettingsPage::onThemeToggled);
    if (m_context && m_context->themeManager())
    {
        const bool dark = m_context->themeManager()->isDark();
        m_themeToggle->setChecked(!dark);
        m_themeToggle->setText(dark ? tr("Switch to light theme") : tr("Switch to dark theme"));
        connect(m_context->themeManager(), &ThemeManager::themeChanged, this,
                [this](AppTheme theme)
                {
                    const QSignalBlocker blocker(m_themeToggle);
                    const bool dark = theme == AppTheme::Dark;
                    m_themeToggle->setChecked(!dark);
                    m_themeToggle->setText(dark ? tr("Switch to light theme") : tr("Switch to dark theme"));
                });
    }
}

void SettingsPage::onThemeToggled(bool lightTheme)
{
    if (m_context && m_context->themeManager())
        m_context->themeManager()->setTheme(lightTheme ? AppTheme::Light : AppTheme::Dark);
}
