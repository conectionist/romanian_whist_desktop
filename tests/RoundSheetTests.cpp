#include <catch2/catch_test_macros.hpp>

#include "GameFixtures.h"

#include "paint/Theme.h"
#include "table/RoundText.h"
#include "ui/RoundSheet.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QStringList>

#include <string>

// The split SnapshotLog.h argues for and NewGameTests follows: the half with a
// right answer tested without a window, the widget after it.
namespace
{
RoundSeatResult seatResult(const std::string& name,
                           int roundScore,
                           int totalScore,
                           unsigned int winStreak = 0,
                           unsigned int lossStreak = 0)
{
    RoundSeatResult seat;
    seat.name = name;
    seat.hasBet = true;
    seat.bet = 2;
    seat.tricksWon = 2;
    seat.roundScore = roundScore;
    seat.totalScore = totalScore;
    seat.winStreak = winStreak;
    seat.lossStreak = lossStreak;
    return seat;
}

// A round whose NEXT round counts for streaks, which is the ordinary case - the
// exception is what the warning cases below turn off deliberately.
RoundResult roundResult(std::vector<RoundSeatResult> seats)
{
    RoundResult result;
    result.roundNumber = 7;
    result.roundCount = 26;
    result.trickCount = 6;
    result.roundType = romanian_whist::RoundType::Normal;
    result.nextRoundCountsForStreaks = true;
    result.seats = std::move(seats);
    return result;
}
}

TEST_CASE("The sheet's title names the round it reports", "[round-sheet]")
{
    const RoundResult result = roundResult({ seatResult("Ana", 7, 34) });

    const QString title = roundsheet::titleFor(result);

    REQUIRE(title.contains(QStringLiteral("7")));

    // THE CARD COUNT IN THE INFO STRIP'S OWN WORDS, which is what stops the
    // sheet growing a second spelling of it. The first version built its own
    // "%1 cards" and drew "Round 1 - 1 cards - Normal" directly beneath a strip
    // that said "1 card" - found by rendering it, and this is what would catch
    // the copy coming back.
    REQUIRE(title.contains(infostrip::cardsText(6)));

    RoundResult oneCard = result;
    oneCard.trickCount = 1;

    REQUIRE(roundsheet::titleFor(oneCard).contains(infostrip::cardsText(1)));
    REQUIRE_FALSE(roundsheet::titleFor(oneCard).contains(QStringLiteral("1 cards")));

    // THE ROUND TYPE IN THE INFO STRIP'S OWN WORDS, not a second spelling. Two
    // switches over RoundType is exactly the drift section 7 warns about, and
    // this is what would catch a copy being made.
    REQUIRE(title.contains(infostrip::roundTypeText(romanian_whist::RoundType::Normal)));

    RoundResult forehead = result;
    forehead.roundType = romanian_whist::RoundType::Forehead;

    REQUIRE(roundsheet::titleFor(forehead)
                .contains(infostrip::roundTypeText(romanian_whist::RoundType::Forehead)));
}

TEST_CASE("The arrow shows the total before and after this round", "[round-sheet]")
{
    // SECTION 16.1's "34 -> 41", and the reason it is an arrow rather than one
    // number: inside onRoundScored() the total has NOT been folded into yet, so
    // either number alone reads differently on the two sides of the commit.
    REQUIRE(roundsheet::arrowFor(seatResult("Ana", 7, 34)).contains(QStringLiteral("34")));
    REQUIRE(roundsheet::arrowFor(seatResult("Ana", 7, 34)).contains(QStringLiteral("41")));

    // A LOSING ROUND GOES DOWN, which is the case an implementation that summed
    // the wrong way round still passes above - 34 and 41 both appear whichever
    // way they are ordered, and 34 -> 33 does not.
    const QString down = roundsheet::arrowFor(seatResult("Ana", -1, 34));
    REQUIRE(down.contains(QStringLiteral("33")));
    REQUIRE_FALSE(down.contains(QStringLiteral("35")));
}

TEST_CASE("The round score carries its own sign", "[round-sheet]")
{
    // SECTION 18: no cue is colour alone. The widget colours this column as
    // well, and the sign is what survives a player who cannot tell the two
    // colours apart.
    REQUIRE(roundsheet::roundScoreFor(seatResult("Ana", 7, 0)) == QStringLiteral("+7"));

    // SECTION 2.13's MINUS SIGN, U+2212, and not a hyphen: it is as wide as the
    // plus in tabular figures, so the column of scores lines up.
    REQUIRE(roundsheet::roundScoreFor(seatResult("Ana", -1, 0)) == QStringLiteral("\u22121"));

    // Zero went neither way and takes neither sign.
    REQUIRE(roundsheet::roundScoreFor(seatResult("Ana", 0, 0)) == QStringLiteral("0"));
}

TEST_CASE("A negative total takes the round column's minus sign", "[round-sheet]")
{
    // PR #36's review, finding #6. The Round column wrote U+2212 and the Total
    // column a hyphen, so the two minus signs of one sheet differed in glyph and
    // width beside each other.
    REQUIRE(roundsheet::arrowFor(seatResult("Ana", 5, -3)) == QStringLiteral("\u22123 \u2192 2"));
    REQUIRE(roundsheet::arrowFor(seatResult("Ana", -4, 1)) == QStringLiteral("1 \u2192 \u22123"));
}

TEST_CASE("The line under the title names the trump or says there is none", "[round-sheet]")
{
    // The mock-up's .meta line: the trump the round was played to, and how it was
    // scored, for a player reading the column of scores under it.
    RoundResult result = roundResult({ seatResult("Ana", 7, 34) });
    result.hasTrump = true;
    result.trump = romanian_whist::Card(romanian_whist::Rank::King, romanian_whist::Suit::Hearts);

    const QString meta = roundsheet::metaFor(result);

    REQUIRE(meta.startsWith(QStringLiteral("Trump K\u2665")));
    REQUIRE(meta.contains(QStringLiteral("exact bid scores 5 + bid")));

    // AN EIGHT-CARD ROUND HAS NO TRUMP, and `trump` is then Card's default - a real
    // two of clubs - which the sheet must not name.
    result.hasTrump = false;

    REQUIRE(roundsheet::metaFor(result).startsWith(QStringLiteral("No trump")));
    REQUIRE_FALSE(roundsheet::metaFor(result).contains(QStringLiteral("2")));
}

TEST_CASE("The sheet's table has the mock-up's five headings", "[round-sheet]")
{
    RoundSheet sheet;
    sheet.showResult(roundResult({ seatResult("Ana", 7, 34) }));

    QStringList headings;

    for(const QLabel* label : sheet.findChildren<QLabel*>())
    {
        for(const char* word : { "Player", "Bid", "Taken", "Round", "Total" })
        {
            if(label->text() == QString::fromUtf8(word))
                headings << label->text();
        }
    }

    REQUIRE(headings.size() == 5);

    // The sign in red and green as well as in the text: the row's round score.
    const QLabel* score = nullptr;

    for(const QLabel* label : sheet.findChildren<QLabel*>())
    {
        if(label->text() == QStringLiteral("+7"))
            score = label;
    }

    REQUIRE(score != nullptr);
    REQUIRE(score->palette().color(QPalette::WindowText) == Theme::dark().table.good);
}

TEST_CASE("The streak warning fires on four, and only on four", "[round-sheet]")
{
    // The engine pays out on the FIFTH, so four is the round at which a player
    // can still do something about it - and the counts already include the round
    // being reported, because calculateScores() updates them before
    // onRoundScored() fires.
    const auto warnsAtLoss = [](unsigned int streak)
    {
        return !roundsheet::streakWarnings(
                    roundResult({ seatResult("Ana", -1, 30, 0, streak) })).isEmpty();
    };

    REQUIRE_FALSE(warnsAtLoss(0));
    REQUIRE_FALSE(warnsAtLoss(3));
    REQUIRE(warnsAtLoss(4));

    // FIVE HAS ALREADY PAID OUT. A warning there would be telling the player
    // about something that has happened rather than something coming.
    REQUIRE_FALSE(warnsAtLoss(5));
}

TEST_CASE("The streak warning names the seat and both directions", "[round-sheet]")
{
    const QStringList missing =
        roundsheet::streakWarnings(roundResult({ seatResult("Ana", -1, 30, 0, 4) }));

    REQUIRE(missing.size() == 1);
    REQUIRE(missing.at(0).contains(QStringLiteral("Ana")));
    REQUIRE(missing.at(0).contains(QStringLiteral("10")));

    const QStringList hitting =
        roundsheet::streakWarnings(roundResult({ seatResult("Bogdan", 7, 30, 4, 0) }));

    REQUIRE(hitting.size() == 1);
    REQUIRE(hitting.at(0).contains(QStringLiteral("Bogdan")));

    // The two are different sentences: one is a cost and the other a reward, and
    // a warning that read the same either way would be worse than none.
    REQUIRE(missing.at(0) != hitting.at(0));
}

TEST_CASE("Every seat on four is warned, not just the first", "[round-sheet]")
{
    // D2'S REVIEW FINDING. The first version returned from inside the loop, so a
    // round where two players are both on four warned about one of them - and
    // the other never learned that their fifth costs or earns 10, which is the
    // whole reason this payload is built on the game thread at all.
    //
    // ONE OF EACH DIRECTION, which is what also catches the sharper half of the
    // bug: an earlier seat's WIN streak used to silence a later seat's LOSS
    // streak, and a fix that collected the seats but reused one sentence for
    // both would still be wrong.
    const QStringList lines = roundsheet::streakWarnings(
        roundResult({ seatResult("Ana", 7, 30, 4, 0),
                      seatResult("Bogdan", 3, 30),        // on neither
                      seatResult("Cristina", -1, 30, 0, 4) }));

    REQUIRE(lines.size() == 2);

    REQUIRE(lines.at(0).contains(QStringLiteral("Ana")));
    REQUIRE(lines.at(1).contains(QStringLiteral("Cristina")));

    // The seat on neither is not named anywhere in either line.
    REQUIRE_FALSE(lines.join(QLatin1Char('\n')).contains(QStringLiteral("Bogdan")));

    // Ana is up and Cristina is down, so these must be the two DIFFERENT
    // sentences rather than one sentence with two names in it.
    REQUIRE(lines.at(0) != lines.at(1));
}

TEST_CASE("The streak warning is silent when the next round deals one card", "[round-sheet]")
{
    // THE CASE THE WHOLE PAYLOAD EXISTS FOR, and the one that is wrong in a way
    // no eye would catch: one-card rounds count for NEITHER direction of a
    // streak - they neither advance one nor break it - so "a fifth costs 10" is
    // simply false when the next round is one.
    //
    // The flag is the only way to know, because it is a fact about the NEXT
    // round: no snapshot carries it, and the GUI thread may not ask the engine.
    RoundResult result = roundResult({ seatResult("Ana", -1, 30, 0, 4) });

    REQUIRE_FALSE(roundsheet::streakWarnings(result).isEmpty());

    result.nextRoundCountsForStreaks = false;
    REQUIRE(roundsheet::streakWarnings(result).isEmpty());
}

TEST_CASE("The sheet draws a row for every seat, and rebuilds between rounds",
          "[round-sheet]")
{
    // The widget half, constructible because TestApp is a QApplication on the
    // offscreen platform.
    RoundSheet sheet;

    sheet.showResult(roundResult({ seatResult("Ana", 7, 34),
                                   seatResult("Bogdan", -1, 34),
                                   seatResult("Cristina", 5, 23) }));

    const auto labelsSaying = [&sheet](const QString& text)
    {
        int found = 0;
        for(const QLabel* label : sheet.findChildren<QLabel*>())
            if(label->text() == text)
                found++;
        return found;
    };

    REQUIRE(labelsSaying(QStringLiteral("Ana")) == 1);
    REQUIRE(labelsSaying(QStringLiteral("Cristina")) == 1);

    // A SECOND ROUND REPLACES THE FIRST rather than appending to it. The grid is
    // reused across every round of a game, and QGridLayout::rowCount() never
    // shrinks - which is the trap clearRows() counts its own rows to avoid.
    sheet.showResult(roundResult({ seatResult("Ana", 2, 41),
                                   seatResult("Bogdan", -3, 33),
                                   seatResult("Cristina", 5, 28) }));

    REQUIRE(labelsSaying(QStringLiteral("Ana")) == 1);

    // And a game with FEWER seats after one with more, which is what a second
    // game through the same window can be.
    sheet.showResult(roundResult({ seatResult("Ana", 1, 43) }));

    REQUIRE(labelsSaying(QStringLiteral("Ana")) == 1);
    REQUIRE(labelsSaying(QStringLiteral("Cristina")) == 0);
}

TEST_CASE("The sheet shows the streak warning only when there is one", "[round-sheet]")
{
    RoundSheet sheet;

    const auto warningLabel = [&sheet]
    {
        return sheet.findChild<QLabel*>(QStringLiteral("streakWarning"));
    };

    sheet.showResult(roundResult({ seatResult("Ana", 7, 34) }));
    REQUIRE(warningLabel() != nullptr);
    REQUIRE_FALSE(warningLabel()->isVisible());

    sheet.showResult(roundResult({ seatResult("Ana", -1, 34, 0, 4) }));
    REQUIRE(warningLabel()->text().contains(QStringLiteral("Ana")));

    // TWO SEATS ON FOUR PUT BOTH IN THE LABEL, joined rather than one of them
    // dropped - the widget half of the case above.
    sheet.showResult(roundResult({ seatResult("Ana", -1, 34, 0, 4),
                                   seatResult("Bogdan", 7, 34, 4, 0) }));

    REQUIRE(warningLabel()->text().contains(QStringLiteral("Ana")));
    REQUIRE(warningLabel()->text().contains(QStringLiteral("Bogdan")));

    // AND IT GOES AWAY AGAIN. A warning left standing from the round before is
    // worse than none at all - it names a streak that has just been broken.
    sheet.showResult(roundResult({ seatResult("Ana", 7, 33) }));
    REQUIRE(warningLabel()->text().isEmpty());
}

TEST_CASE("The round sheet's quiet Round history sits before Continue", "[round-sheet]")
{
    // The mock-up's [Round history] beside its deal button (phase 11): the whole
    // game so far, without taking Enter from Continue.
    RoundSheet sheet;
    sheet.showResult(roundResult({ seatResult("Ana", 7, 34) }));

    QPushButton* history = sheet.findChild<QPushButton*>("roundHistory");
    QPushButton* continueButton = sheet.findChild<QPushButton*>("continueRound");
    REQUIRE(history != nullptr);
    REQUIRE(continueButton != nullptr);

    REQUIRE(static_cast<SheetButton*>(history)->kind() == SheetButton::Kind::Quiet);
    REQUIRE(continueButton->isDefault());
    REQUIRE_FALSE(history->isDefault());
    REQUIRE_FALSE(history->autoDefault());

    sheet.resize(1280, 754);
    sheet.show();
    QCoreApplication::processEvents();

    REQUIRE(history->geometry().right() < continueButton->geometry().left());

    int asked = 0;
    QObject::connect(&sheet, &RoundSheet::historyRequested, [&asked] { asked++; });

    history->click();
    REQUIRE(asked == 1);
}
