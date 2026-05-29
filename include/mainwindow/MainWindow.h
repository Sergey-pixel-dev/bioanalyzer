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

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void setMainMenuVisible(bool visible);

private:
    Ui::MainWindow *ui;
    MainWindowPresentationModel *m_pm;
};

#endif // MAINWINDOW_H
