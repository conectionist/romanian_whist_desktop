#include <catch2/catch_test_macros.hpp>

#include "MainWindow.h"
#include "table/HandItem.h"
#include "table/TableScene.h"
#include "ui/BidStrip.h"
#include "ui/NewGameDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <functional>
#include <iostream>

// Screenshots of the real window, for comparing the client against the table
// redesign's mock-up (docs/ui-redesign/screenshots/). A TOOL, NOT A TEST: the
// tag is hidden, so neither a bare run nor ctest picks it up and the suite's
// count does not move. Run it by tag:
//
//     WHIST_SHOTS_DIR=/some/dir ./build/tests/desktop_tests "[.screenshot]"
//
// IN THE TEST BINARY because that is where everything it needs already is: the
// offscreen QApplication TestApp.cpp builds, and the card art, which a separate
// target would have to compile from cards.qrc a third time.
//
// THE SAME STATE ON EVERY RUN, which is what makes a before and an after
// comparable. The deal is fixed, every bot plays a strategy that draws nothing,
// and the human answers by a fixed rule - so the n-th decision the human is
// asked for is always the same moment of the same game. A shot is named by that
// count rather than by the round and trick it lands on, because the prompts are
// what the redesign promises to keep and the status bar is not; the status line
// is printed beside each shot for whoever has to know which moment it shows.
namespace
{
// The same two loops as MainWindowTests.cpp's, and for its reason: queued
// snapshots are delivered by the event loop, so waiting means pumping.
void pump(int ms)
{
    QElapsedTimer clock;
    clock.start();

    while(clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool pumpUntil(int limitMs, const std::function<bool()>& ready)
{
    QElapsedTimer clock;
    clock.start();

    while(clock.elapsed() < limitMs)
    {
        if(ready())
            return true;

        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    return ready();
}

struct Shot
{
    const char* file;
    int players;

    // How many decisions the human makes before the picture is taken: 0 is the
    // first bid of round 1. Bids and cards both count.
    int decisions;
};

// Picked by running this and looking. Each table shot is the human's turn
// mid-game with every opponent's card already down - one more than the mock-up's
// table-4-players.jpg shows, so every seat's played card has a position to
// compare. The bidding shot is round 1's opening bid, as in bidding.jpg.
constexpr Shot shots[] = {
    { "table-3-players.png", 3, 26 },   // round 8, trick 2 of 6, 5 in hand
    { "table-4-players.png", 4, 22 },   // round 8, trick 2 of 5, 4 in hand
    { "table-5-players.png", 5, 24 },   // round 9, trick 2 of 5, 4 in hand
    { "table-6-players.png", 6, 26 },   // round 10, trick 2 of 5, 4 in hand
    { "bidding.png",         6, 0 },    // round 1, the human opens
};

// Answers the setup dialog from inside its nested event loop, as
// MainWindowTests.cpp's answerModal* helpers do. A played game with a fixed deal,
// dealt as fast as the client can deal one.
//
// THE BOTS ARE SET, NOT LEFT AT THE DIALOG'S DEFAULTS. The defaults include a
// Reckoner, which samples - seeded it replays, but it is also slow, and a change
// to the defaults would quietly move every shot. LowRisk and Ducking draw
// nothing, which is GameFixtures.h's rule for a game that has to repeat.
void answerModalShotGame(int afterMs, int players)
{
    QTimer::singleShot(afterMs, [players]
    {
        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)
            return;

        if(QRadioButton* playing = dialog->findChild<QRadioButton*>("playing"))
            playing->setChecked(true);

        // BEFORE the levels: the rows follow the opponent count.
        if(QSpinBox* opponents = dialog->findChild<QSpinBox*>("opponents"))
            opponents->setValue(players - 1);

        for(int i = 0 ; i < players - 1 ; i++)
        {
            const newgame::BotLevel level = i % 2 == 0 ? newgame::BotLevel::LowRisk
                                                       : newgame::BotLevel::Ducking;

            if(QComboBox* combo = dialog->findChild<QComboBox*>(QStringLiteral("botLevel%1").arg(i)))
                combo->setCurrentIndex(static_cast<int>(level));
        }

        if(QCheckBox* fixedDeal = dialog->findChild<QCheckBox*>("fixedDeal"))
            fixedDeal->setChecked(true);

        if(QSpinBox* seed = dialog->findChild<QSpinBox*>("seed"))
            seed->setValue(42);

        if(QSlider* speed = dialog->findChild<QSlider*>("speed"))
            speed->setValue(newgame::FastestSpeed);

        if(QCheckBox* wait = dialog->findChild<QCheckBox*>("waitEachRound"))
            wait->setChecked(false);

        dialog->done(QDialog::Accepted);
    });
}

// The first slot a click would play, or slotCount() when there is none.
std::size_t firstPlayableSlot(const HandItem& hand)
{
    std::size_t slot = 0;

    while(slot < hand.slotCount() && !hand.isPlayable(slot))
        slot++;

    return slot;
}

// Clicks the lowest bid the strip offers. The forbidden bid is a disabled
// button, so "lowest enabled" is "lowest legal".
bool clickLowestBid(const BidStrip& strip)
{
    QPushButton* lowest = nullptr;

    for(QPushButton* button : strip.findChildren<QPushButton*>())
    {
        bool isNumber = false;
        const int bet = button->text().toInt(&isNumber);

        if(isNumber && button->isEnabled() && (!lowest || bet < lowest->text().toInt()))
            lowest = button;
    }

    if(!lowest)
        return false;

    lowest->click();
    return true;
}
}

TEST_CASE("Screenshots of the table for the redesign", "[.screenshot]")
{
    const QString dir = qEnvironmentVariable("WHIST_SHOTS_DIR", QStringLiteral("shots"));
    REQUIRE(QDir().mkpath(dir));

    for(const Shot& shot : shots)
    {
        DYNAMIC_SECTION(shot.file)
        {
            MainWindow window;
            window.resize(1920, 1080);
            window.show();
            pump(200);

            answerModalShotGame(300, shot.players);
            window.findChild<QPushButton*>("newGame")->click();

            BidStrip* bidStrip = window.findChild<BidStrip*>();
            TableScene* scene = window.findChild<TableScene*>();
            REQUIRE(bidStrip != nullptr);
            REQUIRE(scene != nullptr);

            HandItem* hand = scene->handItem();

            for(int decision = 0 ; ; decision++)
            {
                const bool asked = pumpUntil(20000, [&]
                {
                    return bidStrip->isVisible() || firstPlayableSlot(*hand) < hand->slotCount();
                });

                // A target past the end of the game lands here, with the last
                // thing the window said, rather than hanging.
                INFO("decision " << decision << ", last status: "
                     << window.statusBar()->currentMessage().toStdString());
                REQUIRE(asked);

                if(decision == shot.decisions)
                    break;

                if(bidStrip->isVisible())
                    REQUIRE(clickLowestBid(*bidStrip));
                else
                    hand->activate(firstPlayableSlot(*hand));
            }

            // The engine is parked on the prompt, so nothing moves while the
            // last frame settles.
            pump(200);

            const QPixmap picture = window.grab();
            REQUIRE(picture.size() == QSize(1920, 1080));

            const QString path = QDir(dir).filePath(QString::fromUtf8(shot.file));
            REQUIRE(picture.save(path));

            std::cout << path.toStdString() << ": "
                      << window.statusBar()->currentMessage().toStdString() << '\n';

            REQUIRE(window.close());
        }
    }
}
