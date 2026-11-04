#include "navigation/MainWindow.h"
#include "navigation/SettingsPage.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Biosignal Analyzer"));
    QApplication::setOrganizationName(QStringLiteral("Biosignal"));

    // Язык выбирается на странице настроек и применяется при запуске, до
    // создания окон: формы и надписи берут переведённые строки сразу.
    QTranslator qtTranslator;
    QTranslator appTranslator;
    const QString language = SettingsPage::savedLanguage();
    if (language != QStringLiteral("en"))
    {
        // Кнопки стандартных окон (QMessageBox, QFileDialog) — из перевода Qt.
        if (qtTranslator.load(QLocale(language), QStringLiteral("qtbase"), QStringLiteral("_"),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QApplication::installTranslator(&qtTranslator);
        if (appTranslator.load(QStringLiteral(":/i18n/biosignal_%1.qm").arg(language)))
            QApplication::installTranslator(&appTranslator);
    }
    MainWindow w;
    w.show();
    return a.exec();
}
