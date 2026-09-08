#include "mainwindow/MainWindowPresentationModel.h"
#include <QSettings>

MainWindowPresentationModel::MainWindowPresentationModel(QObject *parent)
    : QObject(parent)
{
}

void MainWindowPresentationModel::toggleMainMenu()
{
    setMainMenuVisible(!m_mainMenuVisible);
}

void MainWindowPresentationModel::setMainMenuVisible(bool visible)
{
    if (m_mainMenuVisible == visible)
        return;
    m_mainMenuVisible = visible;
    emit mainMenuVisibleChanged(visible);
}

void MainWindowPresentationModel::setAiExpanded(bool expanded)
{
    if (m_aiExpanded == expanded)
        return;
    m_aiExpanded = expanded;
    emit aiExpandedChanged(expanded);
}

void MainWindowPresentationModel::selectPage(int page)
{
    // Negative indices identify navigation groups. They only toggle the
    // group's presentation state and never change the content page.
    if (page < 0)
    {
        setAiExpanded(!m_aiExpanded);
        return;
    }
    if (m_currentPage == page)
        return;
    m_currentPage = page;
    emit currentPageChanged(page);
}

void MainWindowPresentationModel::setSplitterState(const QByteArray &state)
{
    m_splitterState = state;
}

void MainWindowPresentationModel::restore(QSettings &settings)
{
    setMainMenuVisible(settings.value(QStringLiteral("navigation/mainMenuVisible"), true).toBool());
    setAiExpanded(settings.value(QStringLiteral("navigation/aiExpanded"), true).toBool());
    setSplitterState(settings.value(QStringLiteral("navigation/splitterState")).toByteArray());
}

void MainWindowPresentationModel::save(QSettings &settings) const
{
    settings.setValue(QStringLiteral("navigation/mainMenuVisible"), m_mainMenuVisible);
    settings.setValue(QStringLiteral("navigation/aiExpanded"), m_aiExpanded);
    settings.setValue(QStringLiteral("navigation/splitterState"), m_splitterState);
}
