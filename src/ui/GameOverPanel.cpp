#include "GameOverPanel.h"

#include <QBoxLayout>
#include <QLabel>

#include <algorithm>

using romanian_whist::Standing;

namespace gameover
{

QString winnerAnnouncement(const std::vector<Standing>& winners)
{
    if(winners.empty())
        return {};

    // Every winner is on the same score by construction, so the front one's is
    // the score the sentence names however many there are.
    const QString points = QString::number(winners.front().score);

    if(winners.size() == 1)
        return QCoreApplication::translate("gameover", "%1 wins with %2 points.")
            .arg(QString::fromStdString(winners.front().name), points);

    // "Ana and Bogdan", "Ana, Bogdan and Cristi" - the last name joined with
    // "and" rather than a comma, however many there are.
    //
    // THE SEPARATORS ARE TRANSLATED TOO, and that is the one thing this gained
    // over the terminal's version: a language that does not join a list with
    // ", " and " and " needs both, and a comma written into the loop would be
    // the string lupdate never sees.
    QString names;

    for(std::size_t i = 0 ; i < winners.size() ; i++)
    {
        if(i > 0)
            names += (i + 1 == winners.size()
                          ? QCoreApplication::translate("gameover", " and ")
                          : QCoreApplication::translate("gameover", ", "));

        names += QString::fromStdString(winners[i].name);
    }

    return QCoreApplication::translate("gameover", "%1 tie for first with %2 points.")
        .arg(names, points);
}

} // namespace gameover

GameOverPanel::GameOverPanel(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    title = new QLabel(panel);
    title->setObjectName("gameOverTitle");
    dress(title, sheet::Role::Title);

    inside->addWidget(title);

    // The one line that has to survive a long name at a six-seat table - it names
    // every winner of a draw, so it can be four names long. dress() wraps it.
    announcement = new QLabel(panel);
    announcement->setObjectName("announcement");
    dress(announcement, sheet::Role::Meta);

    inside->addSpacing(2);
    inside->addWidget(announcement);
    inside->addSpacing(16);

    // The mock-up's standings: Place, Player, Total - and the star, which needs no
    // heading.
    rows = new SheetTable(panel);
    rows->setObjectName("standings");
    rows->setColumns({ { tr("Place"), Qt::AlignLeft },
                       { tr("Player"), Qt::AlignLeft },
                       { tr("Total"), Qt::AlignRight },
                       { QString(), Qt::AlignLeft } });

    inside->addWidget(rows);

    // NOT "mainMenu", WHICH IS THE PAUSE MENU'S. Both panels are children of the
    // same stack and both offer this button, so the plain name would make
    // findChild<QPushButton*>("mainMenu") answer with whichever the widget tree
    // reaches first - and a test that clicked the wrong one would still pass,
    // since the two are wired to the same slot. D1's rule that every control a
    // test reaches for is named is only worth anything if the name is unique.
    mainMenu = new SheetButton(tr("Main menu"), SheetButton::Kind::Quiet, panel);
    mainMenu->setObjectName("gameOverMainMenu");
    mainMenu->setAutoDefault(false);

    // ON BOTH FACES: a game that ended in an error still played the rounds it
    // scored. Never the default.
    history = new SheetButton(tr("Round history"), SheetButton::Kind::Quiet, panel);
    history->setObjectName("gameOverHistory");
    history->setAutoDefault(false);

    playAgain = new SheetButton(tr("Play again"), SheetButton::Kind::Primary, panel);
    playAgain->setObjectName("playAgain");
    playAgain->setDefault(true);

    // SIDE BY SIDE at the bottom right, the mock-up's sheets' place for them:
    // two ways on from a game that has ended, the green one last, and a look back
    // between them.
    //
    // NO Close BUTTON, departing from section 16.2's mockup and recorded there.
    // That mockup predates D1's main-menu page: the window's X and the menu
    // page's Quit both already end the session, and D1 replaced the menu bar
    // with a page precisely so that leaving a GAME is not leaving the
    // APPLICATION.
    actions = addActions({ mainMenu, history, playAgain });

    connect(playAgain, &QPushButton::clicked, this, &GameOverPanel::playAgainRequested);
    connect(history, &QPushButton::clicked, this, &GameOverPanel::historyRequested);
    connect(mainMenu, &QPushButton::clicked, this, &GameOverPanel::mainMenuRequested);
}

void GameOverPanel::showResult(const std::vector<Standing>& standings,
                               const std::vector<Standing>& winners)
{
    // A second game over must not append to the first one's table.
    rows->clearRows();

    title->setText(tr("Final results"));

    for(const Standing& standing : standings)
    {
        // BY SEAT, against the getWinners() payload - not by asking whether the
        // place is 1, and not by starring the top row. The engine decides who
        // won; every other way of answering is this client deciding it again,
        // which is the shape of the bug the terminal shipped in 2.1.0.
        const bool isWinner =
            std::any_of(winners.begin(), winners.end(),
                        [&standing](const Standing& winner)
                        { return winner.seat == standing.seat; });

        addRow(standing, isWinner);
    }

    rows->show();
    announcement->setText(gameover::winnerAnnouncement(winners));

    playAgain->show();
    playAgain->setDefault(true);

    mainMenu->setKind(SheetButton::Kind::Quiet);
    mainMenu->setDefault(false);
    placeMainMenu(false);

    placeSheet();
}

void GameOverPanel::showFailure(const QString& what)
{
    rows->clearRows();

    // NO STANDINGS ON THIS FACE, because there are none: the run slot threw, so
    // the round it landed in is unscored and the engine was never asked for a
    // final table. The message takes the announcement's place.
    rows->hide();

    title->setText(tr("The game ended in an error"));
    announcement->setText(what);

    // AND NOTHING TO PLAY AGAIN FROM. A game can be started again from the menu
    // - the controller is reusable and prepare() rebuilds everything - but a
    // button offering to repeat what just failed, with the same settings, is
    // not the offer to make about a thread that caught something. Main menu is
    // then the one way on, and the green one.
    playAgain->hide();
    mainMenu->setKind(SheetButton::Kind::Primary);
    mainMenu->setDefault(true);
    placeMainMenu(true);

    placeSheet();
}

void GameOverPanel::placeMainMenu(bool last)
{
    // THE GREEN ONE LAST, as on every sheet: on the failure face Main menu is the
    // green one, so it moves past Round history (PR #37's review, finding #1). The
    // Tab order moves with it.
    actions->removeWidget(mainMenu);

    const int at = actions->indexOf(history);
    actions->insertWidget(last ? at + 1 : at, mainMenu);

    if(last)
        QWidget::setTabOrder(history, mainMenu);
    else
        QWidget::setTabOrder(mainMenu, history);
}

void GameOverPanel::focusDefault()
{
    if(playAgain->isVisible())
        playAgain->setFocus(Qt::OtherFocusReason);
    else
        mainMenu->setFocus(Qt::OtherFocusReason);
}

void GameOverPanel::addRow(const Standing& standing, bool isWinner)
{
    // A GLYPH AND NOT A COLOUR, unlike the terminal's yellow row. Section 18
    // wants no fact carried by colour alone, and the star is the mark section
    // 16.2's mockup already uses; the bold is on top of it rather than instead.
    //
    // NOT tr()'d, and that is deliberate rather than an omission: it is a symbol
    // and not a word, so there is nothing for a translator to do with it but
    // break it. The terminal's capability check does not port either - it exists
    // because a TERMINAL may have no Unicode at all, and Qt substitutes a font
    // per character.
    const QString star = isWinner ? QStringLiteral("★") : QString();

    rows->addRow({ { tr("%1.").arg(standing.place), SheetTable::Tone::Plain, isWinner },
                   { QString::fromStdString(standing.name), SheetTable::Tone::Plain, isWinner },
                   { sheet::number(standing.score), SheetTable::Tone::Plain, isWinner },
                   { star, SheetTable::Tone::Plain, isWinner } });
}
