#include "mainwindow/MainWindow.h"

#include <QApplication>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Biosignal Analyzer"));
    QApplication::setOrganizationName(QStringLiteral("Biosignal"));
    MainWindow w;
    w.show();
    return a.exec();
}
