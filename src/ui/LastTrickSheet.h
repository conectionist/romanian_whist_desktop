#ifndef LAST_TRICK_SHEET_H
#define LAST_TRICK_SHEET_H

#include "core/GameSnapshot.h"
#include "ui/Overlay.h"

#include <romanian_whist/Card.h>

#include <QRectF>
#include <QSizeF>
#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class CardRenderer;

// THE LAST TRICK (the table redesign's phase 11): the trick most recently won in
// the game, its cards in the order they were played, each over its player's name
// and the winner's ringed in gold. The mock-up's showLastTrick(), but not only this
// round's (see Tracker).
//
// Translated through QCoreApplication::translate() under the context "lasttrick",
// for the reason table/ScoreboardItem.h gives.
namespace lasttrick
{

// One card of a trick as the sheet shows it.
struct Played
{
    romanian_whist::Card card{};
    unsigned int seatIndex = 0;
    QString name;

    // The engine's ranking, never recomputed (PlayedCardView::isWinning): on a
    // finished trick, the card that took it.
    bool isWinning = false;
};

struct Trick
{
    // The round it was won in, 1-based as the snapshot counts.
    unsigned int roundNumber = 0;

    // Your seat, or -1 when you are watching: whose win is "You took it".
    int humanSeat = -1;

    // In play order, the leader's first.
    std::vector<Played> cards;
};

// WHAT THE WINDOW KEEPS OF THE TABLE: the last trick finished, in whichever round,
// read off the frames as they pass.
//
// A FINISHED TRICK IS A FRAME WITH A CARD FROM EVERY SEAT. The bridge publishes one
// as the trick is won, while it is still the engine's current trick
// (GameBridge::onTrickWon()), and its isWinning is the engine's winner. Taken the
// moment it is complete, so while it is still on the felt the sheet shows that
// same trick; the frames of the next trick, partly played, leave it be.
//
// KEPT ACROSS ROUNDS, by the owner's call of 2026-10-22: "Last trick" reads as the
// last trick played, and at the start of a round that is the last round's final
// trick. The mock-up forgets it when it deals, and so did phase 11 at first, which
// left the button saying nothing had finished at every round's start. Only a new
// game forgets it, through clear().
class Tracker
{
public:
    void observe(const GameSnapshot& snapshot);
    void clear() { kept.reset(); }

    const std::optional<Trick>& last() const { return kept; }

private:
    std::optional<Trick> kept;
};

// THE LINE UNDER THE TITLE: who took it - "You took it", or "Ana took it" - and,
// for a trick from a round before `currentRound`, which one: "Ana took it · round
// 4". With no trick finished in the game yet, "No trick has finished yet.". Your
// own seat is "You", as in every sentence on the table (section 2.10); a game you
// watch has no "You". Whole sentences, so a translator can agree the verb.
QString metaFor(const std::optional<Trick>& trick, unsigned int currentRound);

// .sheet .mini: each card 88 wide at the art's own shape, 12 apart, and its
// player's name in .note's 15 under it, 6 below the card. The winner is ringed
// 2 wide in gold-bright and glows 16 round it.
inline constexpr qreal cardWidth = 88.0;
inline constexpr qreal cardGap = 12.0;
inline constexpr qreal cardRadius = 6.0;
inline constexpr qreal nameAbove = 6.0;
inline constexpr qreal ringWidth = 2.0;
inline constexpr qreal glowBlur = 16.0;

// The size of a card in the sheet: 88 wide, as tall as the art makes it
// (CardRenderer::sizeForHeight()) - with or without a renderer to draw it.
QSizeF cardSize();

// The i-th card's rect, from the row's top-left.
QRectF cardRect(std::size_t i, QSizeF card);

} // namespace lasttrick

// The widget, an Overlay like every sheet the table's corner buttons and the menu
// bar open: over everything, and holding a game in play while it is up.
class LastTrickSheet : public Overlay
{
    Q_OBJECT

public:
    explicit LastTrickSheet(QWidget* parent = nullptr);

    // The table's card art, which this draws from and whose pictures it shares.
    // Until it is given one the sheet draws its names but no cards.
    void setCards(const CardRenderer* renderer);

    // Fills the sheet: the trick's cards, or - with none finished yet - the line
    // that says so, and no cards. `currentRound` is the round on the table, which
    // says whether the trick needs its round named.
    void showTrick(const std::optional<lasttrick::Trick>& trick, unsigned int currentRound);

    // Close, so Enter closes.
    void focusDefault() override;

    // WHAT THE SHEET SHOWS, for tests: the trick, and the rect its row of cards
    // takes in the sheet's body.
    const std::optional<lasttrick::Trick>& shownTrick() const { return shown; }
    QRect cardsArea() const;

signals:
    void closeRequested();

protected:
    // The row of cards is painted on the sheet's body, under the place a widget
    // keeps for it: the winner's glow reaches past that place, into the sheet's
    // padding, as the mock-up's box-shadow does.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void paintCards(QPainter& painter) const;

    // How big the row is for the trick shown.
    QSize rowSize() const;

    const CardRenderer* renderer = nullptr;
    std::optional<lasttrick::Trick> shown;

    QLabel* title = nullptr;
    QLabel* meta = nullptr;

    // Holds the row's place in the sheet's column. Paints nothing itself.
    QWidget* row = nullptr;

    SheetButton* closeButton = nullptr;
};

#endif
