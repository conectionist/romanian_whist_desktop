#include "MainWindow.h"

#include "engine/MetaTypes.h"
#include "paint/Fonts.h"

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

    // After the QApplication, which owns the font database, and before
    // MainWindow, so no widget ever lays text out without these faces.
    fonts::registerBundled();

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

    // MAXIMIZED, the owner's call of 2026-10-23: the table is drawn at the window's
    // scale, and a maximized window draws it at its largest while keeping the title
    // bar and the taskbar. Restoring it gives the .ui's size, raised to the
    // window's minimum.
    w.showMaximized();
    return QApplication::exec();
}
