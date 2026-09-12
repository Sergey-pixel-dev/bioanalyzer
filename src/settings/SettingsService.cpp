#include "settings/SettingsService.h"

#include <QSettings>

namespace
{
constexpr auto kRecentDatasetsKey = "ai/recentDatasets";
constexpr auto kRecentModelsKey   = "ai/recentModels";
constexpr int  kRecentLimit       = 10;
}

SettingsService::SettingsService(QObject *parent) : QObject(parent)
{
    QSettings s;
    m_recentDatasets = s.value(kRecentDatasetsKey).toStringList();
    m_recentModels   = s.value(kRecentModelsKey).toStringList();
}

void SettingsService::rememberDataset(const QString &path)
{
    if (path.isEmpty())
        return;
    m_recentDatasets.removeAll(path);
    m_recentDatasets.prepend(path);
    while (m_recentDatasets.size() > kRecentLimit)
        m_recentDatasets.removeLast();
    QSettings().setValue(kRecentDatasetsKey, m_recentDatasets);
    emit recentDatasetsChanged();
}

void SettingsService::rememberModel(const QString &path)
{
    if (path.isEmpty())
        return;
    m_recentModels.removeAll(path);
    m_recentModels.prepend(path);
    while (m_recentModels.size() > kRecentLimit)
        m_recentModels.removeLast();
    QSettings().setValue(kRecentModelsKey, m_recentModels);
    emit recentModelsChanged();
}
