#include "MainWindow.h"

#include "engine/MetaTypes.h"
#include "paint/Fonts.h"
#include "ui/LanguageDialog.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLocale>
#include <QTranslator>

#include <optional>

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

    // THE LANGUAGE, BEFORE ANYTHING IS BUILT IN IT (the redesign's phase 13, the
    // owner's call of 2026-10-23): --language en|ro says it, and without one the
    // player is asked. Chosen first, every string the window makes is made in it,
    // and nothing has to be translated again while the app runs. The system's
    // language only picks which answer the dialog starts on.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Romanian Whist, the desktop client."));
    parser.addHelpOption();

    const QCommandLineOption languageOption(QStringLiteral("language"),
                                            QStringLiteral("Start in this language without asking: en (English) "
                                                           "or ro (Romanian)."),
                                            QStringLiteral("code"));
    parser.addOption(languageOption);
    parser.process(a);

    std::optional<language::Language> chosen;

    if(parser.isSet(languageOption))
    {
        chosen = language::fromCode(parser.value(languageOption));

        if(!chosen)
            qWarning("Unknown language \"%s\": use en or ro. Asking instead.",
                     qPrintable(parser.value(languageOption)));
    }

    if(!chosen)
    {
        LanguageDialog ask(language::fromSystem(QLocale::system().uiLanguages()));

        // Closed without an answer: there is nothing to show yet, so the app ends.
        if(ask.exec() != QDialog::Accepted)
            return 0;

        chosen = ask.choice();
    }

    QTranslator translator;
    const QString resource = language::translationResource(*chosen);

    if(!resource.isEmpty())
    {
        if(translator.load(resource))
            QApplication::installTranslator(&translator);
        else
            qWarning("Could not load the translation %s: starting in English.", qPrintable(resource));
    }

    MainWindow w;

    // MAXIMIZED, the owner's call of 2026-10-23: the table is drawn at the window's
    // scale, and a maximized window draws it at its largest while keeping the title
    // bar and the taskbar. Restoring it gives the .ui's size, raised to the
    // window's minimum.
    w.showMaximized();
    return QApplication::exec();
}
