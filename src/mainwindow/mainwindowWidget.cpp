#include "../../include/mainwindow/mainwindowWidget.h"
#include "ui_mainwindow.h"

MainWindowWidget::MainWindowWidget(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindowWidget)
{
    ui->setupUi(this);
    ui->MainMenu->addItem("Monitoring");
    ui->MainMenu->addItem("Protocol");
}

MainWindowWidget::~MainWindowWidget()
{
    delete ui;
}
