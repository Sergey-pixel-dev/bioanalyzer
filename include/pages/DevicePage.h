#ifndef DEVICEPAGE_H
#define DEVICEPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
    class DevicePage;
}
QT_END_NAMESPACE

class DevicePage : public QWidget
{
    Q_OBJECT
public:
    explicit DevicePage(QWidget *parent = nullptr);
    ~DevicePage();

private:
    Ui::DevicePage *ui;
};

#endif // DEVICEPAGE_H
