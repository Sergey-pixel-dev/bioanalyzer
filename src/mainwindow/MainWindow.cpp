#include "mainwindow/MainWindow.h"
#include "ui_mainwindow.h"
#include "mainwindow/MainWindowPresentationModel.h"
#include "pages/MonitoringPage.h"
#include "pages/DevicePage.h"
#include "app/AppContext.h"
#include "pages/AiPages.h"
#include <QSplitter>
#include <QTreeWidget>
#include <QSettings>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
      m_pm(new MainWindowPresentationModel(this)),
      m_context(new AppContext(this))
{
    ui->setupUi(this);
    // Replace the designer's flat navigation with a collapsible tree inside
    // a mouse-resizable splitter.
    while (ui->horizontalLayout->count()) delete ui->horizontalLayout->takeAt(0);
    auto *splitter = new QSplitter(Qt::Horizontal, ui->centralwidget);
    auto *nav = ui->MainMenu;
    auto *content = new QWidget(splitter);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0,0,0,0);
    contentLayout->addWidget(ui->toolButtonVisibility, 0, Qt::AlignLeft);
    contentLayout->addWidget(ui->stackedWidget, 1);
    splitter->addWidget(nav); splitter->addWidget(content);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 760});
    ui->horizontalLayout->addWidget(splitter);
    nav->clear();
    auto *monitor = new QTreeWidgetItem(nav, {tr("Monitoring")}); monitor->setData(0, Qt::UserRole, 0);
    auto *devices = new QTreeWidgetItem(nav, {tr("Devices")}); devices->setData(0, Qt::UserRole, 1);
    auto *ai = new QTreeWidgetItem(nav, {tr("AI")}); ai->setData(0, Qt::UserRole, -1);
    auto *collect = new QTreeWidgetItem(ai, {tr("Data Collection")}); collect->setData(0, Qt::UserRole, 2);
    auto *training = new QTreeWidgetItem(ai, {tr("Training")}); training->setData(0, Qt::UserRole, 3);
    auto *inference = new QTreeWidgetItem(ai, {tr("Inference")}); inference->setData(0, Qt::UserRole, 4);
    ai->setExpanded(QSettings().value("navigation/aiExpanded", true).toBool());
    nav->setCurrentItem(monitor);
    splitter->restoreState(QSettings().value("navigation/splitterState").toByteArray());
    nav->setMinimumWidth(180); nav->setMaximumWidth(420);
    connect(nav, &QTreeWidget::itemClicked, this, &MainWindow::onNavigationItem);
    connect(nav, &QTreeWidget::itemExpanded, this, [ai](QTreeWidgetItem *it){ if(it==ai) QSettings().setValue("navigation/aiExpanded", true); });
    connect(nav, &QTreeWidget::itemCollapsed, this, [ai](QTreeWidgetItem *it){ if(it==ai) QSettings().setValue("navigation/aiExpanded", false); });
    MonitoringPage *monitoringPage = new MonitoringPage(m_context, this);
    DevicePage *devicePage = new DevicePage(m_context, this);
    DataCollectionPage *dataPage = new DataCollectionPage(m_context, this);
    TrainingPage *trainingPage = new TrainingPage(m_context, this);
    InferencePage *inferencePage = new InferencePage(m_context, this);
    ui->stackedWidget->insertWidget(0, monitoringPage);
    ui->stackedWidget->insertWidget(1, devicePage);
    ui->stackedWidget->insertWidget(2, dataPage);
    ui->stackedWidget->insertWidget(3, trainingPage);
    ui->stackedWidget->insertWidget(4, inferencePage);

    connect(ui->toolButtonVisibility, &QToolButton::clicked,
            m_pm, &MainWindowPresentationModel::toggleMainMenu);

    connect(m_pm, &MainWindowPresentationModel::mainMenuVisibleChanged,
            this, &MainWindow::setMainMenuVisible);

    setMainMenuVisible(m_pm->isMainMenuVisible());
}

MainWindow::~MainWindow()
{
    if (auto *splitter = findChild<QSplitter*>()) QSettings().setValue("navigation/splitterState", splitter->saveState());
    delete ui;
}

void MainWindow::onNavigationItem(QTreeWidgetItem *item, int)
{
    if (!item) return;
    const int page = item->data(0, Qt::UserRole).toInt();
    if (page >= 0) ui->stackedWidget->setCurrentIndex(page);
    else if (item->childCount() > 0) { item->setExpanded(!item->isExpanded()); }
}

void MainWindow::setMainMenuVisible(bool visible)
{
    ui->MainMenu->setVisible(visible);
    ui->toolButtonVisibility->setArrowType(
        visible ? Qt::LeftArrow : Qt::RightArrow);
}
