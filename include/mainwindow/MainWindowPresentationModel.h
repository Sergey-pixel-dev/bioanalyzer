#ifndef MAINWINDOW_PRESENTATION_MODEL_H
#define MAINWINDOW_PRESENTATION_MODEL_H

#include <QObject>

class MainWindowPresentationModel : public QObject
{
    Q_OBJECT
public:
    explicit MainWindowPresentationModel(QObject *parent = nullptr);

    bool isMainMenuVisible() const { return m_mainMenuVisible; }

public slots:
    void toggleMainMenu();
    void setMainMenuVisible(bool visible);

signals:
    void mainMenuVisibleChanged(bool visible);

private:
    bool m_mainMenuVisible = true;
};

#endif // MAINWINDOW_PRESENTATION_MODEL_H
