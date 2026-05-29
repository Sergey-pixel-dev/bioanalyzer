#include "mainwindow/MainWindow.h"
#include "ui_mainwindow.h"
#include "mainwindow/MainWindowPresentationModel.h"
#include "pages/MonitoringPage.h"
#include "pages/DevicePage.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_pm(new MainWindowPresentationModel(this))
{
    ui->setupUi(this);
    MonitoringPage *monitoringPage = new MonitoringPage(this);
    DevicePage *devicePage = new DevicePage(this);
    ui->stackedWidget->insertWidget(0, monitoringPage);
    ui->stackedWidget->insertWidget(1, devicePage);

    QObject::connect(ui->MainMenu, &QListWidget::currentRowChanged, ui->stackedWidget, &QStackedWidget::setCurrentIndex);

    connect(ui->toolButtonVisibility, &QToolButton::clicked,
            m_pm, &MainWindowPresentationModel::toggleMainMenu);

    connect(m_pm, &MainWindowPresentationModel::mainMenuVisibleChanged,
            this, &MainWindow::setMainMenuVisible);

    setMainMenuVisible(m_pm->isMainMenuVisible());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setMainMenuVisible(bool visible)
{
    ui->MainMenu->setVisible(visible);
    ui->toolButtonVisibility->setArrowType(
        visible ? Qt::LeftArrow : Qt::RightArrow);
}
