#include "mainwindow/MainWindowPresentationModel.h"

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
