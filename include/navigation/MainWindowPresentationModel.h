#ifndef MAINWINDOW_PRESENTATION_MODEL_H
#define MAINWINDOW_PRESENTATION_MODEL_H

#include <QObject>
#include <QByteArray>

class QSettings;

class MainWindowPresentationModel : public QObject
{
    Q_OBJECT
public:
    explicit MainWindowPresentationModel(QObject *parent = nullptr);

    bool isMainMenuVisible() const { return m_mainMenuVisible; }
    bool isAiExpanded() const { return m_aiExpanded; }
    int currentPage() const { return m_currentPage; }
    const QByteArray &splitterState() const { return m_splitterState; }

    void restore(QSettings &settings);
    void save(QSettings &settings) const;

public slots:
    void toggleMainMenu();
    void setMainMenuVisible(bool visible);
    void setAiExpanded(bool expanded);
    void selectPage(int page);
    void setSplitterState(const QByteArray &state);

signals:
    void mainMenuVisibleChanged(bool visible);
    void aiExpandedChanged(bool expanded);
    void currentPageChanged(int page);

private:
    bool m_mainMenuVisible = true;
    bool m_aiExpanded = true;
    int m_currentPage = 0;
    QByteArray m_splitterState;
};

#endif // MAINWINDOW_PRESENTATION_MODEL_H
