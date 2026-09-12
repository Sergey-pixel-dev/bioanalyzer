#ifndef SETTINGS_SETTINGSSERVICE_H
#define SETTINGS_SETTINGSSERVICE_H

#include <QObject>
#include <QStringList>

// Persistent application-scope preferences.
//
// В текущей версии хранит списки последних использованных датасетов и
// обученных моделей. При добавлении новых предпочтений (пути по
// умолчанию, размер окна и т. п.) их следует помещать сюда, а не в
// AppContext, чтобы модуль navigation не превращался в свалку настроек.
class SettingsService : public QObject
{
    Q_OBJECT
public:
    explicit SettingsService(QObject *parent = nullptr);

    const QStringList &recentDatasets() const { return m_recentDatasets; }
    const QStringList &recentModels() const { return m_recentModels; }

    void rememberDataset(const QString &path);
    void rememberModel(const QString &path);

signals:
    void recentDatasetsChanged();
    void recentModelsChanged();

private:
    QStringList m_recentDatasets;
    QStringList m_recentModels;
};

#endif // SETTINGS_SETTINGSSERVICE_H
