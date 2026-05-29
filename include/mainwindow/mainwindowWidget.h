#ifndef MAINWINDOWWIDGET_H
#define MAINWINDOWWIDGET_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindowWidget;
}
QT_END_NAMESPACE

class MainWindowWidget : public QMainWindow
{
    Q_OBJECT

public:
    MainWindowWidget(QWidget *parent = nullptr);
    ~MainWindowWidget();

private:
    Ui::MainWindowWidget *ui;
};

#endif // MAINWINDOWWIDGET_H
