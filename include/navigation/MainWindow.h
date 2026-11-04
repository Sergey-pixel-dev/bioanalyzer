#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindowPresentationModel;
class QTreeWidgetItem;
class QSplitter;
class AppContext;
class QtDeviceSessionAdapter;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void applyMainMenuVisible(bool visible);
    void applyAiExpanded(bool expanded);
    void applyCurrentPage(int page);
    void onNavigationItem(QTreeWidgetItem *item, int column);

private:
    // Отказы устройства выводятся службой сообщений над главным окном, на
    // какой бы странице ни был оператор: сообщение об ошибке команды и
    // сообщение о потере связи.
    void watchSession(QtDeviceSessionAdapter *session);

    Ui::MainWindow *ui;
    MainWindowPresentationModel *m_pm;
    AppContext *m_context;
    QSplitter *m_splitter = nullptr;
    QTreeWidgetItem *m_aiItem = nullptr;
    QPointer<QtDeviceSessionAdapter> m_watchedSession;
};

#endif // MAINWINDOW_H
