#include "mainwindow/MainWindow.h"
#include "ui_mainwindow.h"
#include "mainwindow/MainWindowPresentationModel.h"
#include "pages/MonitoringPage.h"
#include "pages/DevicePage.h"
#include "app/AppContext.h"
#include "pages/AiPages.h"
#include "pages/SettingsPage.h"
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
    while (ui->horizontalLayout->count())
        delete ui->horizontalLayout->takeAt(0);
    m_splitter = new QSplitter(Qt::Horizontal, ui->centralwidget);
    auto *nav = ui->MainMenu;
    auto *content = new QWidget(m_splitter);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(ui->toolButtonVisibility, 0, Qt::AlignLeft);
    contentLayout->addWidget(ui->stackedWidget, 1);
    m_splitter->addWidget(nav);
    m_splitter->addWidget(content);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 760});
    ui->horizontalLayout->addWidget(m_splitter);
    nav->clear();
    auto *monitor = new QTreeWidgetItem(nav, {tr("Monitoring")});
    monitor->setData(0, Qt::UserRole, 0);
    m_aiItem = new QTreeWidgetItem(nav, {tr("AI")});
    m_aiItem->setData(0, Qt::UserRole, -1);
    auto *collect = new QTreeWidgetItem(m_aiItem, {tr("Data Collection")});
    collect->setData(0, Qt::UserRole, 2);
    auto *training = new QTreeWidgetItem(m_aiItem, {tr("Training")});
    training->setData(0, Qt::UserRole, 3);
    auto *inference = new QTreeWidgetItem(m_aiItem, {tr("Inference")});
    inference->setData(0, Qt::UserRole, 4);
    auto *devices = new QTreeWidgetItem(nav, {tr("Devices")});
    devices->setData(0, Qt::UserRole, 1);
    auto *settingsItem = new QTreeWidgetItem(nav, {tr("Settings")});
    settingsItem->setData(0, Qt::UserRole, 5);
    nav->setCurrentItem(monitor);
    nav->setMinimumWidth(180);
    nav->setMaximumWidth(420);
    connect(nav, &QTreeWidget::itemClicked, this, &MainWindow::onNavigationItem);
    connect(nav, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it)
            { if (it == m_aiItem) m_pm->setAiExpanded(true); });
    connect(nav, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *it)
            { if (it == m_aiItem) m_pm->setAiExpanded(false); });
    MonitoringPage *monitoringPage = new MonitoringPage(m_context, this);
    DevicePage *devicePage = new DevicePage(m_context, this);
    DataCollectionPage *dataPage = new DataCollectionPage(m_context, this);
    TrainingPage *trainingPage = new TrainingPage(m_context, this);
    InferencePage *inferencePage = new InferencePage(m_context, this);
    SettingsPage *settingsPage = new SettingsPage(m_context, this);
    ui->stackedWidget->insertWidget(0, monitoringPage);
    ui->stackedWidget->insertWidget(1, devicePage);
    ui->stackedWidget->insertWidget(2, dataPage);
    ui->stackedWidget->insertWidget(3, trainingPage);
    ui->stackedWidget->insertWidget(4, inferencePage);
    ui->stackedWidget->insertWidget(5, settingsPage);

    connect(ui->toolButtonVisibility, &QToolButton::clicked,
            m_pm, &MainWindowPresentationModel::toggleMainMenu);

    connect(m_pm, &MainWindowPresentationModel::mainMenuVisibleChanged,
            this, &MainWindow::applyMainMenuVisible);
    connect(m_pm, &MainWindowPresentationModel::aiExpandedChanged,
            this, &MainWindow::applyAiExpanded);
    connect(m_pm, &MainWindowPresentationModel::currentPageChanged,
            this, &MainWindow::applyCurrentPage);

    QSettings persistedSettings;
    m_pm->restore(persistedSettings);
    applyMainMenuVisible(m_pm->isMainMenuVisible());
    applyAiExpanded(m_pm->isAiExpanded());
    applyCurrentPage(m_pm->currentPage());
    if (!m_pm->splitterState().isEmpty())
        m_splitter->restoreState(m_pm->splitterState());
}

MainWindow::~MainWindow()
{
    if (m_splitter)
        m_pm->setSplitterState(m_splitter->saveState());
    QSettings settings;
    m_pm->save(settings);
    delete ui;
}

void MainWindow::onNavigationItem(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const int page = item->data(0, Qt::UserRole).toInt();

    m_pm->selectPage(page);
}

void MainWindow::applyMainMenuVisible(bool visible)
{
    ui->MainMenu->setVisible(visible);
    ui->toolButtonVisibility->setArrowType(
        visible ? Qt::LeftArrow : Qt::RightArrow);
}

void MainWindow::applyAiExpanded(bool expanded)
{
    if (m_aiItem && m_aiItem->isExpanded() != expanded)
        m_aiItem->setExpanded(expanded);
}

void MainWindow::applyCurrentPage(int page)
{
    if (page >= 0 && page < ui->stackedWidget->count())
        ui->stackedWidget->setCurrentIndex(page);
}
