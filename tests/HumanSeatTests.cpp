#include <catch2/catch_test_macros.hpp>

#include "GameFixtures.h"

#include "engine/GameAbandoned.h"
#include "engine/GameBridge.h"
#include "engine/GameController.h"
#include "engine/QtMoveProvider.h"


#include <romanian_whist/GameEngine.h>
#include <romanian_whist/RoundType.h>
#include <romanian_whist/Seat.h>

#include <QCoreApplication>
#include <QObject>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace romanian_whist;

// C1: what changes when a seat has a person in it.
//
// Phases A and B were spectator games throughout, which means three things had
// never run at all before this phase: GameController::start() filling a seat,
// QtMoveProvider parking in a real game, and the half of refreshFromEngine()
// that needs a viewer to answer canSeeHand(). All three are silent when wrong -
// a client that dropped humanSeat renders a correct table with an empty hand -
// so they are asserted here rather than looked at.
//
// TWO TESTS AND TWO SHAPES, deliberately.
//
// The first drives a real GameController on a real thread and asks one question:
// does a SEATED game reach onGameOver? Every existing controller test is a
// spectator game or a deliberate failure, so nothing yet proves the positive
// path through start()'s seat-filling.
//
// The rest run the engine SYNCHRONOUSLY on this thread, the way A6's sequence
// tests do, because what they are about - which prompt carried what, against
// which snapshot - is not a threading question and gains nothing from a thread
// but the chance to hang. Answering a prompt from inside its own emission is
// sanctioned rather than tolerated: QtMoveProvider drops its lock across the
// emit precisely so submit() can be called on the emitting thread.

namespace
{
constexpr std::size_t kSeats = 4;

// NOT SEAT 0, for the reason MainWindow's own kHumanSeat is not: seat 0 is where
// a spectator game already puts its first seat, so a client or a test that lost
// track of the human entirely would agree with seat 0 and with nothing else.
constexpr std::size_t kHumanSeat = 1;

// What a prompt asked, kept so the whole stream can be asserted over after the
// game rather than one prompt at a time inside it.
struct BetRecord
{
    unsigned int promptId = 0;
    std::size_t handSize = 0;
    RoundType roundType = RoundType::Normal;
    std::size_t cardsShown = 0;

    // Engaged for the final bidder only, and only while the value is still in
    // range. Recorded so a case can assert the rule was EXERCISED - a whole game
    // in which no prompt ever carried one says nothing about honouring it.
    std::optional<unsigned int> forbidden;
};

struct PlayRecord
{
    unsigned int promptId = 0;
    bool blind = false;
    std::size_t entries = 0;
    std::size_t handIndex = 0;
    bool anyLegal = false;
};

struct Transcript
{
    std::vector<BetRecord> bets;
    std::vector<PlayRecord> plays;
    std::vector<GameSnapshot> frames;

    // Set when the ceiling below was hit and the game was abandoned out from
    // under the engine rather than allowed to run on. See kPromptCeiling.
    bool looped = false;
};

// THE DEADLINE THIS TEST WOULD OTHERWISE NOT HAVE, and it is not theoretical:
// it was found by mutating chooseSlot() to ignore legality, which does not fail
// this file - it HANGS it. makeBet() and playCard() both re-prompt rather than
// pass an illegal answer to the engine, so an answerer that keeps giving the
// same wrong one is asked again forever, the game never ends, and every
// assertion below is unreachable. The suite is killed with no output, which
// GameControllerTests already calls a far worse way to learn an assertion broke
// than being told.
//
// Well above any real game's needs - the whole schedule is a few hundred
// decisions - so this is only ever reached by a loop.
constexpr std::size_t kPromptCeiling = 5000;

// Plays a whole seated game on THIS thread and returns everything it was asked
// and everything it published.
//
// The setup's human seat is left null by buildSetup() and filled here with a
// SeatProvider - the non-owning forwarder, exactly as GameController::start()
// does it, and for the same reason: GameSetup owns its providers and destroys
// them with the engine, so the QtMoveProvider that a click handler points at
// must not be the object in there.
Transcript playSeatedGame()
{
    Transcript transcript;

    GameBridge bridge(Seat{ static_cast<unsigned int>(kHumanSeat) }, fixtures::instant());
    QtMoveProvider provider(&bridge);

    // Past the ceiling, stop answering and wake the provider so it throws
    // instead. abandon() is the documented way out of a parked provider and it
    // LATCHES, so this is safe to call from inside the emission the provider is
    // waiting on - which is exactly where a loop leaves us.
    const auto exhausted = [&]
    {
        if(transcript.bets.size() + transcript.plays.size() < kPromptCeiling)
            return false;

        transcript.looped = true;
        provider.abandon();

        return true;
    };

    // No receiver context, so these are direct: the handler runs on the thread
    // raising the prompt, which is this one.
    QObject::connect(&bridge, &GameBridge::betRequested,
                     [&](BetPrompt prompt)
                     {
                         transcript.bets.push_back(BetRecord{ prompt.promptId,
                                                              prompt.handSize,
                                                              prompt.roundType,
                                                              prompt.hand.size(),
                                                              prompt.forbiddenBet });

                         if(exhausted())
                             return;

                         provider.submit(prompt.promptId, fixtures::lowestLegalBid(prompt));
                     });

    QObject::connect(&bridge, &GameBridge::cardRequested,
                     [&](PlayPrompt prompt)
                     {
                         const bool anyLegal =
                             std::any_of(prompt.entries.begin(), prompt.entries.end(),
                                         [](const HandEntry& entry){ return entry.legal; });

                         transcript.plays.push_back(PlayRecord{ prompt.promptId,
                                                                prompt.blind,
                                                                prompt.entries.size(),
                                                                prompt.handIndex.size(),
                                                                anyLegal });

                         if(exhausted())
                             return;

                         // THROUGH handIndex, which is the translation the whole
                         // prompt shape exists for and the one a click handler
                         // will make. Submitting the slot itself would play a
                         // different card and no engine rule would object.
                         const std::size_t slot = fixtures::firstLegalSlot(prompt);
                         provider.submit(prompt.promptId, prompt.handIndex[slot]);
                     });

    QObject::connect(&bridge, &GameBridge::stateChanged,
                     [&](std::shared_ptr<const GameSnapshot> snapshot)
                     { transcript.frames.push_back(*snapshot); });

    GameSetup setup = fixtures::buildSetup(kSeats, 42u, kHumanSeat);
    // .at(), matching GameController::start(): a seat past the end of the table
    // is an exception rather than undefined behaviour, and this file's seat count
    // and human seat are two constants that could drift apart.
    setup.seats.at(kHumanSeat).moveProvider = std::make_unique<SeatProvider>(&provider);

    GameEngine engine;
    engine.addObserver(&bridge);
    engine.start(std::move(setup));

    try
    {
        engine.run();
    }
    catch(const GameAbandoned&)
    {
        // Only ever the ceiling above: nothing else in this file abandons. The
        // engine is left mid-round and unresumable, which is fine - it is
        // destroyed on the next line either way - and `looped` is what the
        // assertion below reports.
    }

    // THE FLOOR THE CASES BELOW STAND ON, asserted once rather than in each of
    // them - they index and divide by these, and a game that ended early would
    // otherwise pass several of them vacuously.
    //
    // FIRST, because it is the one that explains the rest. A stand-in that
    // re-prompts forever fails every case below on a short transcript, and this
    // is the line that says why.
    REQUIRE(!transcript.looped);
    REQUIRE(transcript.frames.size() > 500);
    REQUIRE(!transcript.bets.empty());
    REQUIRE(!transcript.plays.empty());

    return transcript;
}

// Every round's trick count, read off the published frames rather than
// re-derived from the structure. The human plays one card per trick, so the sum
// is exactly how many card prompts a game with no re-prompting raises.
//
// The MAXIMUM per round, because the first frame of a round can be published
// before the count is meaningful, and because the final round is published again
// after it is scored.
std::map<unsigned int, unsigned int> trickCounts(const std::vector<GameSnapshot>& frames)
{
    std::map<unsigned int, unsigned int> counts;

    for(const GameSnapshot& frame : frames)
    {
        unsigned int& count = counts[frame.roundNumber];
        count = std::max(count, frame.trickCount);
    }

    return counts;
}
}

TEST_CASE("A seated game reaches game over through the controller", "[humanseat]")
{
    // THE POSITIVE PATH THROUGH start()'s SEAT FILLING, which nothing else
    // covers: every other controller test is a spectator game or a setup the
    // engine is meant to reject.
    //
    // WHAT THIS DOES NOT COVER, said outright because the connection below looks
    // like it does: the answers here are given DIRECT, on the game thread, so
    // nothing crosses GameController's queued relay and no prompt metatype is
    // ever asked to marshal. Deleting both qRegisterMetaType calls AND the
    // betRequested relay leaves this case green - measured, not supposed. The
    // case after it is the one that fails, and the two are kept apart on
    // purpose: this one passing while that one fails says the seat is fine and
    // the relay is not, which is a diagnosis rather than a symptom.
    GameController controller;
    controller.prepare(Seat{ static_cast<unsigned int>(kHumanSeat) }, fixtures::instant());

    REQUIRE(controller.provider() != nullptr);

    // Direct on the bridge, so this runs on the game thread as the prompt is
    // raised - the same inline answer the synchronous cases below make, and the
    // one a queued handler could not make without the event loop pumped.
    QObject context;

    QObject::connect(controller.bridge(), &GameBridge::betRequested, &context,
                     [&controller](BetPrompt prompt)
                     {
                         controller.provider()->submit(prompt.promptId,
                                                       fixtures::lowestLegalBid(prompt));
                     },
                     Qt::DirectConnection);

    QObject::connect(controller.bridge(), &GameBridge::cardRequested, &context,
                     [&controller](PlayPrompt prompt)
                     {
                         const std::size_t slot = fixtures::firstLegalSlot(prompt);
                         controller.provider()->submit(prompt.promptId,
                                                       prompt.handIndex[slot]);
                     },
                     Qt::DirectConnection);

    fixtures::Outcome outcome(controller);

    controller.start(fixtures::buildSetup(kSeats, 42u, kHumanSeat));

    REQUIRE(outcome.await(std::chrono::seconds(30)));

    // ALL THREE COUNTS, not just the first. "The game ended" is also true of one
    // that failed on an unfilled seat and of one that was stopped, and the whole
    // question here is WHICH ending a seated game reaches when its prompts are
    // answered.
    REQUIRE(outcome.failedCount == 0);
    REQUIRE(outcome.stoppedCount == 0);
    REQUIRE(outcome.overCount == 1);

    controller.stop();
}

TEST_CASE("Both prompts reach the GUI through the controller's queued relay", "[humanseat]")
{
    // THE PRODUCTION PATH, and until C1's review nothing exercised it: prompts
    // raised on the game thread, marshalled across the boundary, answered by
    // submit() from the GUI thread while the game thread is parked. That is
    // exactly what MainWindow does.
    //
    // WHAT IT GUARDS is the two relay connects in prepare(). Deleting either one
    // is a TOTAL LOSS THAT PRINTS NOTHING - the human is never asked, the game
    // parks forever, and the whole client is dead - and the suite was green
    // through both until this case existed. Each now fails it.
    //
    // WHAT NO TEST HERE CAN GUARD is the two qRegisterMetaType calls, and that
    // is measured rather than assumed: removing them AND both
    // Q_DECLARE_METATYPE lines leaves this case passing, because Qt 6 builds a
    // QMetaType from any complete type and the queued relay resolves them on its
    // own. Prompts.h says as much beside the macros. They stay for the platform
    // or the future Qt where auto-registration does not reach, where the failure
    // is one line on stderr and a game that hangs on your turn - so the defence
    // is that all four live in two named places, not that anything checks them.
    // Do not read this case as cover for deleting them.
    //
    // THE ASSERTION IS THAT THE GAME ENDS. It can only end if the human is asked
    // and answers, so "reached onGameOver" IS "both relays carried a payload".
    // The counters below only make the failure legible.
    GameController controller;
    controller.prepare(Seat{ static_cast<unsigned int>(kHumanSeat) }, fixtures::instant());

    QObject context;
    std::atomic<int> bets{0};
    std::atomic<int> cards{0};

    // TO THE CONTROLLER, not the bridge, and with no explicit connection type -
    // which is the whole point. The bridge is moved to the game thread by
    // start(), so the relay becomes queued and these handlers run HERE, on the
    // test thread, standing in for the window.
    QObject::connect(&controller, &GameController::betRequested, &context,
                     [&](BetPrompt prompt)
                     {
                         bets++;
                         controller.provider()->submit(prompt.promptId,
                                                       fixtures::lowestLegalBid(prompt));
                     });

    QObject::connect(&controller, &GameController::cardRequested, &context,
                     [&](PlayPrompt prompt)
                     {
                         cards++;
                         const std::size_t slot = fixtures::firstLegalSlot(prompt);
                         controller.provider()->submit(prompt.promptId,
                                                       prompt.handIndex[slot]);
                     });

    fixtures::Outcome outcome(controller);

    // Two seats, so the schedule is short. The point here is the boundary, not
    // the table.
    controller.start(fixtures::buildSetup(2, 42u, kHumanSeat));

    // PUMPED, NOT AWAITED, and Outcome::await() would deadlock in its place. The
    // game thread is parked inside the prompt waiting for an answer that only
    // this thread can deliver, and it can only deliver it by running its event
    // loop - so a blocking wait on the condition variable would sit there while
    // the game sat on the prompt, and the pass would be a 30-second timeout.
    // Every other controller test can await() because a bot game needs nothing
    // from the main thread; a seated one is the opposite case.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while(outcome.overCount == 0 && outcome.stoppedCount == 0 && outcome.failedCount == 0
          && std::chrono::steady_clock::now() < deadline)
    {
        QCoreApplication::processEvents();

        // So the wait is not a hot spin. One millisecond against a schedule of a
        // few hundred prompts is well under a second of added run time.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Named separately so a failure says WHICH relay is missing rather than only
    // that the game did not finish.
    REQUIRE(bets > 0);
    REQUIRE(cards > 0);

    REQUIRE(outcome.failedCount == 0);
    REQUIRE(outcome.stoppedCount == 0);
    REQUIRE(outcome.overCount == 1);

    controller.stop();
}

TEST_CASE("Every frame of a seated game names the human, and only the human", "[humanseat]")
{
    const Transcript transcript = playSeatedGame();

    for(const GameSnapshot& frame : transcript.frames)
    {
        // SET BEFORE THE REFRESH, which is what gives canSeeHand() a viewer.
        // Leave it at its default and every hand in this file's other cases is
        // empty while the table still renders perfectly - which is the failure
        // this asserts against, and it is silent.
        REQUIRE(frame.humanSeat == static_cast<int>(kHumanSeat));
        REQUIRE(frame.seats.size() == kSeats);

        for(std::size_t i = 0 ; i < frame.seats.size() ; i++)
            REQUIRE(frame.seats[i].isHuman == (i == kHumanSeat));
    }
}

TEST_CASE("In a normal round the human sees their own hand and nobody else's", "[humanseat]")
{
    const Transcript transcript = playSeatedGame();

    bool sawOwnHand = false;

    for(const GameSnapshot& frame : transcript.frames)
    {
        if(frame.roundType != RoundType::Normal)
            continue;

        sawOwnHand = sawOwnHand || !frame.hand.empty();

        // canSeeHand() in a Normal round is "only your own", and this is the
        // half of it a spectator game could never exercise. A seat hand filled
        // here would be the client showing a player the table's cards.
        for(std::size_t i = 0 ; i < frame.seats.size() ; i++)
        {
            if(i != kHumanSeat)
                REQUIRE(frame.seats[i].hand.empty());
        }
    }

    REQUIRE(sawOwnHand);
}

TEST_CASE("A Forehead round shows every hand but the human's own", "[humanseat]")
{
    // SECTION 17'S DATA HALF, observable for the first time in C1 and drawn in
    // C4. "You cannot see your own card but you can see everyone else's" is two
    // separate facts on the snapshot, and a client that got one right and the
    // other wrong would look plausible either way round.
    const Transcript transcript = playSeatedGame();

    bool sawForehead = false;
    bool sawAnotherHand = false;

    for(const GameSnapshot& frame : transcript.frames)
    {
        if(frame.roundType != RoundType::Forehead)
            continue;

        sawForehead = true;

        // Never, on any frame of the round: canSeeHand(human, human) is false in
        // Forehead, so this is empty while the engine is waiting on a card - the
        // exact reason the hand a player clicks comes from the prompt.
        REQUIRE(frame.hand.empty());

        for(std::size_t i = 0 ; i < frame.seats.size() ; i++)
        {
            if(i == kHumanSeat)
                continue;

            // Not on EVERY frame - a hand is empty again once its card is played
            // - so this is "at some point in the round", which is what a widget
            // needs to have something to draw.
            sawAnotherHand = sawAnotherHand || !frame.seats[i].hand.empty();
        }
    }

    REQUIRE(sawForehead);
    REQUIRE(sawAnotherHand);
}

TEST_CASE("A Hidden round shows nobody's hand at all", "[humanseat]")
{
    const Transcript transcript = playSeatedGame();

    bool sawHidden = false;

    for(const GameSnapshot& frame : transcript.frames)
    {
        if(frame.roundType != RoundType::Hidden)
            continue;

        sawHidden = true;

        REQUIRE(frame.hand.empty());

        for(const SeatView& seat : frame.seats)
            REQUIRE(seat.hand.empty());
    }

    REQUIRE(sawHidden);
}

TEST_CASE("A blind round's prompts carry the way back and nothing else", "[humanseat]")
{
    // THE TWO ROUNDS EVERY DEFAULT GAME ENDS WITH, and the ones section 8 says a
    // snapshot-driven hand stalls in. The hand is not sent and then not drawn -
    // it is not sent - so what a widget has to work from is a count of positions
    // and nothing more.
    const Transcript transcript = playSeatedGame();

    std::vector<PlayRecord> blind;

    for(const PlayRecord& play : transcript.plays)
    {
        if(play.blind)
            blind.push_back(play);
    }

    // endWithForeheadAndHidden appends exactly one of each, and both are 1-trick
    // rounds, so the human is asked for exactly one card in each.
    // all1GamesAreForehead is left false, so no round in the middle of the
    // schedule is blind.
    REQUIRE(blind.size() == 2);

    for(const PlayRecord& play : blind)
    {
        REQUIRE(play.entries == 0);
        REQUIRE(play.handIndex == 1);
    }

    // And the bid side of the same rule. BetPrompt has no `blind` flag - it
    // carries the round type, which is what makeBet() branches on - so the two
    // blind rounds are found by type and the emptiness is the assertion.
    std::size_t blindBets = 0;

    for(const BetRecord& bet : transcript.bets)
    {
        if(bet.roundType == RoundType::Normal)
        {
            // The whole hand, laid out with no legal set: a bidding screen greys
            // nothing, so every card reads legal and all of them are shown.
            REQUIRE(bet.cardsShown == bet.handSize);
            continue;
        }

        blindBets++;
        REQUIRE(bet.cardsShown == 0);
    }

    REQUIRE(blindBets == 2);
}

TEST_CASE("Every playable prompt is complete and parallel", "[humanseat]")
{
    const Transcript transcript = playSeatedGame();

    for(const PlayRecord& play : transcript.plays)
    {
        if(play.blind)
            continue;

        // entries and handIndex are parallel by construction - HandLayout builds
        // both from one sort - and a widget indexes the second with a slot it
        // read off the first. A pair that disagreed in length would be an
        // out-of-range read on the last card of a hand and nowhere else.
        REQUIRE(play.entries == play.handIndex);

        // playCard() returns without raising a prompt when nothing is legal, so
        // a raised prompt with no legal slot is a state that should not exist -
        // and it is the state in which a correct widget draws a hand the player
        // cannot click.
        REQUIRE(play.anyLegal);
    }
}

TEST_CASE("No prompt was raised twice for one decision", "[humanseat]")
{
    // WHAT A RE-PROMPT LOOKS LIKE FROM OUTSIDE, and the reason to count rather
    // than to watch. makeBet() and playCard() both loop: an answer that breaks
    // the engine's rules is not passed on, it is asked for again. So a stand-in
    // that ignored forbiddenBet, or submitted a display slot instead of a hand
    // position, would still finish the game and still pass every other case in
    // this file - and would raise more prompts than there are decisions.
    //
    // One bid per seat per round, and one card per seat per trick, so the human's
    // share is one bid per round and one card per trick.
    const Transcript transcript = playSeatedGame();

    const std::map<unsigned int, unsigned int> tricks = trickCounts(transcript.frames);

    std::size_t expectedPlays = 0;

    for(const auto& [round, count] : tricks)
    {
        static_cast<void>(round);
        expectedPlays += count;
    }

    REQUIRE(transcript.bets.size() == tricks.size());
    REQUIRE(transcript.plays.size() == expectedPlays);

    // THE TEETH ON THE LINE ABOVE, and it needs them. getForbiddenBet() is
    // engaged for the final bidder only, so a game in which the human never bid
    // last - or never bid last with a value still in range - exercises the rule
    // this count is meant to police exactly zero times, and the count passes
    // vacuously. Mutating chooseBet() to ignore forbiddenBet was caught by
    // PromptStandInTests and NOT by the count, which is what put this here.
    REQUIRE(std::any_of(transcript.bets.begin(), transcript.bets.end(),
                        [](const BetRecord& bet){ return bet.forbidden.has_value(); }));

    // The generation is bumped once per raise and shared between the two kinds
    // of prompt, so every prompt in a game carries a distinct, increasing id -
    // which is what lets submit() tell an answer to one from an answer to the
    // other over a single channel.
    std::vector<unsigned int> ids;

    for(const BetRecord& bet : transcript.bets)
        ids.push_back(bet.promptId);

    for(const PlayRecord& play : transcript.plays)
        ids.push_back(play.promptId);

    std::sort(ids.begin(), ids.end());

    REQUIRE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}
