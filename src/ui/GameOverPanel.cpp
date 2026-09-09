#include "GameOverPanel.h"

#include <QBoxLayout>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>

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

GameOverPanel::GameOverPanel(QWidget* parent) : QWidget(parent)
{
    panel = new QWidget(this);
    panel->setAutoFillBackground(true);

    QVBoxLayout* inside = new QVBoxLayout(panel);
    inside->setContentsMargins(48, 32, 48, 32);

    title = new QLabel(panel);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("gameOverTitle");

    // OFF THE WIDGET'S OWN FONT rather than a point size written here, the rule
    // MainMenuPage records: a HiDPI display or a large system font gets a title
    // that grew with everything else.
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
    titleFont.setBold(true);
    title->setFont(titleFont);

    inside->addWidget(title);
    inside->addSpacing(20);

    rows = new QWidget(panel);
    rows->setObjectName("standings");

    QGridLayout* grid = new QGridLayout(rows);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(4);

    inside->addWidget(rows);
    inside->addSpacing(20);

    announcement = new QLabel(panel);
    announcement->setAlignment(Qt::AlignCenter);
    announcement->setObjectName("announcement");

    // The one line that has to survive a long name at a six-seat table - it
    // names every winner of a draw, so it can be four names long.
    announcement->setWordWrap(true);

    inside->addWidget(announcement);
    inside->addSpacing(24);

    playAgain = new QPushButton(tr("Play again"), panel);
    playAgain->setObjectName("playAgain");
    playAgain->setDefault(true);

    mainMenu = new QPushButton(tr("Main menu"), panel);

    // NOT "mainMenu", WHICH IS THE PAUSE MENU'S. Both panels are children of the
    // same stack and both offer this button, so the plain name would make
    // findChild<QPushButton*>("mainMenu") answer with whichever the widget tree
    // reaches first - and a test that clicked the wrong one would still pass,
    // since the two are wired to the same slot. D1's rule that every control a
    // test reaches for is named is only worth anything if the name is unique.
    mainMenu->setObjectName("gameOverMainMenu");
    mainMenu->setAutoDefault(false);

    // SIDE BY SIDE, which is section 16.2's row and not the pause menu's column
    // - that one offers a way back into the game and a way out of it, and this
    // one offers two ways on from a game that has ended.
    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch(1);

    for(QPushButton* button : { playAgain, mainMenu })
    {
        button->setMinimumWidth(160);
        button->setMinimumHeight(34);
        buttons->addWidget(button);
    }

    buttons->addStretch(1);
    inside->addLayout(buttons);

    // The panel centred both ways, and no bigger than its contents.
    QVBoxLayout* stack = new QVBoxLayout(this);
    stack->addStretch(1);

    QHBoxLayout* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(panel);
    row->addStretch(1);

    stack->addLayout(row);
    stack->addStretch(1);

    // NO Close BUTTON, departing from section 16.2's mockup and recorded there.
    // That mockup predates D1's main-menu page: the window's X and the menu
    // page's Quit both already end the session, and D1 replaced the menu bar
    // with a page precisely so that leaving a GAME is not leaving the
    // APPLICATION.
    connect(playAgain, &QPushButton::clicked, this, &GameOverPanel::playAgainRequested);
    connect(mainMenu, &QPushButton::clicked, this, &GameOverPanel::mainMenuRequested);
}

void GameOverPanel::setTheme(const Theme& _theme)
{
    theme = _theme;

    // Through the palette rather than a stylesheet, the call every other panel
    // makes - section 18.1's rule is that colours come from Theme, and a
    // stylesheet string is a colour constructed somewhere else.
    QPalette scheme = panel->palette();
    scheme.setColor(QPalette::Window, theme.seatPlaque);
    scheme.setColor(QPalette::WindowText, theme.textPrimary);
    panel->setPalette(scheme);

    update();
}

void GameOverPanel::showResult(const std::vector<Standing>& standings,
                               const std::vector<Standing>& winners)
{
    clearRows();

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
}

void GameOverPanel::showFailure(const QString& what)
{
    clearRows();

    // NO STANDINGS ON THIS FACE, because there are none: the run slot threw, so
    // the round it landed in is unscored and the engine was never asked for a
    // final table. The message takes the announcement's place.
    rows->hide();

    title->setText(tr("The game ended in an error"));
    announcement->setText(what);

    // AND NOTHING TO PLAY AGAIN FROM. A game can be started again from the menu
    // - the controller is reusable and prepare() rebuilds everything - but a
    // button offering to repeat what just failed, with the same settings, is
    // not the offer to make about a thread that caught something.
    playAgain->hide();
    mainMenu->setDefault(true);
}

void GameOverPanel::focusDefaultButton()
{
    if(playAgain->isVisible())
        playAgain->setFocus(Qt::OtherFocusReason);
    else
        mainMenu->setFocus(Qt::OtherFocusReason);
}

void GameOverPanel::clearRows()
{
    QGridLayout* grid = qobject_cast<QGridLayout*>(rows->layout());

    // deleteLater() rather than delete: a second game over is built from inside
    // a slot, and the widgets being removed are not the ones being clicked, but
    // the rule is cheap and the exception is not worth reasoning about.
    while(QLayoutItem* item = grid->takeAt(0))
    {
        if(QWidget* widget = item->widget())
            widget->deleteLater();

        delete item;
    }

    // COUNTED HERE RATHER THAN ASKED OF THE LAYOUT, and this is the trap in
    // reusing a QGridLayout: rowCount() reports the rows it has ever ALLOCATED
    // and never shrinks when items are taken out of it. A second game over
    // reading it back would start writing at row 4 of an empty grid.
    placedRows = 0;
}

void GameOverPanel::addRow(const Standing& standing, bool isWinner)
{
    QGridLayout* grid = qobject_cast<QGridLayout*>(rows->layout());
    const int line = placedRows++;

    QLabel* place = new QLabel(tr("%1.").arg(standing.place), rows);
    QLabel* name = new QLabel(QString::fromStdString(standing.name), rows);
    QLabel* score = new QLabel(QString::number(standing.score), rows);

    // Right-aligned, because a column of scores is read down rather than across
    // - the same call the scoreboard's totals make.
    score->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // A GLYPH AND NOT A COLOUR, unlike the terminal's yellow row. Section 18
    // wants no fact carried by colour alone, and the star is the mark section
    // 16.2's mockup already uses; the bold is on top of it rather than instead.
    //
    // NOT tr()'d, and that is deliberate rather than an omission: it is a symbol
    // and not a word, so there is nothing for a translator to do with it but
    // break it. The terminal's capability check does not port either - it exists
    // because a TERMINAL may have no Unicode at all, and Qt substitutes a font
    // per character.
    QLabel* star = new QLabel(isWinner ? QStringLiteral("★") : QString(), rows);

    if(isWinner)
    {
        QFont bold = name->font();
        bold.setBold(true);

        for(QLabel* cell : { place, name, score, star })
            cell->setFont(bold);
    }

    grid->addWidget(place, line, 0);
    grid->addWidget(name,  line, 1);
    grid->addWidget(score, line, 2);
    grid->addWidget(star,  line, 3);
}

void GameOverPanel::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    painter.fillRect(rect(), theme.dimmedOverlay);

    QWidget::paintEvent(event);
}

void GameOverPanel::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}
