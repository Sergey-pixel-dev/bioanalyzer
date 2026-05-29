#include "pages/MonitoringPage.h"
#include "ui_monitoringpage.h"

MonitoringPage::MonitoringPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::MonitoringPage)
{
    ui->setupUi(this);
}

MonitoringPage::~MonitoringPage()
{
    delete ui;
}
