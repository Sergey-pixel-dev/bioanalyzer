#ifndef MONITORINGPAGE_H
#define MONITORINGPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MonitoringPage;
}
QT_END_NAMESPACE

class MonitoringPage : public QWidget
{
    Q_OBJECT
public:
    explicit MonitoringPage(QWidget *parent = nullptr);
    ~MonitoringPage();

private:
    Ui::MonitoringPage *ui;
};

#endif // MONITORINGPAGE_H
