#include "MainWindow.h"

#include "engine/MetaTypes.h"
#include "paint/Fonts.h"
#include "ui/LanguageDialog.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
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
    // player is asked, Romanian lit to start. Chosen first, every string the window
    // makes is made in it, and nothing has to be translated again while the app
    // runs.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Romanian Whist, the desktop client."));
    const QCommandLineOption helpOption = parser.addHelpOption();

    const QCommandLineOption languageOption(QStringLiteral("language"),
                                            QStringLiteral("Start in this language without asking: en (English) "
                                                           "or ro (Romanian)."),
                                            QStringLiteral("code"));
    parser.addOption(languageOption);

    // parse() AND NOT process(): process() ends the app on any option it does not
    // know, where 1.0 ignored its arguments, so a launcher or an old shortcut that
    // passes one would stop it starting. An unknown option, or --language with no
    // code, is warned about and the app carries on, asking.
    if(!parser.parse(QCoreApplication::arguments()))
        qWarning("%s Carrying on.", qPrintable(parser.errorText()));

    // Help is still process()'s to print, --help-all's too, and it ends the app.
    if(parser.isSet(helpOption) || parser.isSet(QStringLiteral("help-all")))
        parser.process(a);

    std::optional<language::Language> chosen;

    // Empty when not given, and when given with no code, which parse() has warned of.
    const QString code = parser.value(languageOption);

    if(!code.isEmpty())
    {
        chosen = language::fromCode(code);

        if(!chosen)
            qWarning("Unknown language \"%s\": use en or ro. Asking instead.", qPrintable(code));
    }

    if(!chosen)
    {
        LanguageDialog ask;

        // Closed without an answer: there is nothing to show yet, so the app ends.
        if(ask.exec() != QDialog::Accepted)
            return 0;

        chosen = ask.choice();
    }

    QTranslator translator;
    const QString resource = language::translationResource(*chosen);
    language::Language running = *chosen;

    if(!resource.isEmpty())
    {
        if(translator.load(resource))
        {
            QApplication::installTranslator(&translator);
        }
        else
        {
            qWarning("Could not load the translation %s: starting in English.", qPrintable(resource));
            running = language::Language::English;
        }
    }

    // FOR WHAT THE TRANSLATION DOES NOT CARRY: the players' names New Game draws.
    // The language the app runs in, not the one asked for: a translation that
    // failed to load starts the app in English, and its names with it (PR #40's
    // review, finding #2).
    language::setCurrent(running);

    MainWindow w;

    // MAXIMIZED, the owner's call of 2026-10-23: the table is drawn at the window's
    // scale, and a maximized window draws it at its largest while keeping the title
    // bar and the taskbar. Restoring it gives the .ui's size, raised to the
    // window's minimum.
    w.showMaximized();
    return QApplication::exec();
}
