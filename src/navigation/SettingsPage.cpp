#include "navigation/SettingsPage.h"

#include "navigation/AppContext.h"
#include "settings/thememanager.h"
#include "notifications/Notifications.h"
#include "ui_settingspage.h"
#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>

namespace
{
    const char *const kLanguageKey = "appearance/language";
}

QString SettingsPage::savedLanguage()
{
    const QString language = QSettings().value(QLatin1String(kLanguageKey), QStringLiteral("en")).toString();
    return language == QStringLiteral("ru") ? language : QStringLiteral("en");
}

SettingsPage::SettingsPage(AppContext *context, QWidget *parent)
    : QWidget(parent), m_context(context)
{
    Ui::SettingsPageForm ui;
    ui.setupUi(this);
    m_themeToggle = ui.themeToggle;
    m_languageCombo = ui.languageCombo;
    // Названия языков — на самих языках, без перевода.
    m_languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_languageCombo->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    m_languageCombo->setCurrentIndex(m_languageCombo->findData(savedLanguage()));
    connect(m_languageCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onLanguageChosen);
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

void SettingsPage::onLanguageChosen(int index)
{
    const QString language = m_languageCombo->itemData(index).toString();
    if (language.isEmpty() || language == savedLanguage())
        return;
    QSettings().setValue(QLatin1String(kLanguageKey), language);
    // Формы созданы на прежнем языке: новый применяется при запуске.
    if (m_context && m_context->notifications())
        m_context->notifications()->info(this, tr("Language"),
                                         tr("The language will change after the application is restarted."));
}

void SettingsPage::onThemeToggled(bool lightTheme)
{
    if (m_context && m_context->themeManager())
        m_context->themeManager()->setTheme(lightTheme ? AppTheme::Light : AppTheme::Dark);
}
