#include "MainWindow.h"

#include "engine/MetaTypes.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // BEFORE any connect(), which is the whole requirement. Nothing connects
    // this early today - MainWindow does not drive a game until A5 - but the
    // call belongs at the top of main() rather than wherever the first
    // connection happens to be added later.
    registerMetaTypes();

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "romanian_whist_desktop_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return QApplication::exec();
}
