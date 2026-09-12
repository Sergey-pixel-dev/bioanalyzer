#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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
    Ui::MainWindow *ui;
    MainWindowPresentationModel *m_pm;
    AppContext *m_context;
    QSplitter *m_splitter = nullptr;
    QTreeWidgetItem *m_aiItem = nullptr;
};

#endif // MAINWINDOW_H
