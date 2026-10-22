#include "LastTrickSheet.h"

#include "paint/CardRenderer.h"
#include "paint/Fonts.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>

#include <algorithm>
#include <cmath>

// ---- lasttrick:: ----

void lasttrick::Tracker::observe(const GameSnapshot& snapshot)
{
    // ONLY A FINISHED TRICK: a card from every seat. Partly played tables, the
    // next trick's included, leave the kept one as it is.
    if(snapshot.seats.empty() || snapshot.table.size() != snapshot.seats.size())
        return;

    Trick trick;
    trick.roundNumber = snapshot.roundNumber;
    trick.humanSeat = snapshot.humanSeat;

    for(const PlayedCardView& view : snapshot.table)
    {
        Played played;
        played.card = view.card;
        played.seatIndex = view.seatIndex;
        played.isWinning = view.isWinning;

        if(view.seatIndex < snapshot.seats.size())
            played.name = QString::fromStdString(snapshot.seats[view.seatIndex].name);

        trick.cards.push_back(played);
    }

    kept = std::move(trick);
}

QString lasttrick::metaFor(const std::optional<Trick>& trick, unsigned int currentRound)
{
    if(!trick || trick->cards.empty())
        return QCoreApplication::translate("lasttrick", "No trick has finished yet.");

    const auto winner = std::find_if(trick->cards.begin(), trick->cards.end(),
                                     [](const Played& played) { return played.isWinning; });

    // The engine names a winner for every finished trick; a frame that somehow
    // carried none says nothing rather than guessing one.
    if(winner == trick->cards.end())
        return QString();

    const bool yours = trick->humanSeat >= 0
                    && static_cast<unsigned int>(trick->humanSeat) == winner->seatIndex;

    // THE ROUND NAMED WHEN IT IS NOT THIS ONE: at the start of a round the last
    // trick is the last round's, and the cards alone do not say so.
    if(trick->roundNumber != currentRound)
    {
        const QString round = QString::number(trick->roundNumber);

        if(yours)
            return QCoreApplication::translate("lasttrick", "You took it · round %1").arg(round);

        // One arg() taking both, so a name with a %2 in it is not filled in.
        return QCoreApplication::translate("lasttrick", "%1 took it · round %2").arg(winner->name, round);
    }

    if(yours)
        return QCoreApplication::translate("lasttrick", "You took it");

    return QCoreApplication::translate("lasttrick", "%1 took it").arg(winner->name);
}

QSizeF lasttrick::cardSize(const CardRenderer& cards)
{
    // The art's own shape, asked of the one place that knows it, at a height
    // large enough that rounding does not bend it.
    const QSize probe = cards.sizeForHeight(1000.0);

    return QSizeF(cardWidth, cardWidth * probe.height() / probe.width());
}

QRectF lasttrick::cardRect(std::size_t i, QSizeF card)
{
    return QRectF(QPointF(static_cast<qreal>(i) * (card.width() + cardGap), 0.0), card);
}

// ---- the widget ----

namespace
{
// Without the art, the row keeps a card's place at the shape the client's art has.
QSizeF cardSizeOf(const CardRenderer* renderer)
{
    if(renderer)
        return lasttrick::cardSize(*renderer);

    return QSizeF(lasttrick::cardWidth, lasttrick::cardWidth * 242.6669922 / 167.0869141);
}
}

LastTrickSheet::LastTrickSheet(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    title = new QLabel(QCoreApplication::translate("lasttrick", "Last trick"), panel);
    title->setObjectName("lastTrickTitle");
    dress(title, sheet::Role::Title);

    inside->addWidget(title);

    meta = new QLabel(panel);
    meta->setObjectName("lastTrickMeta");
    dress(meta, sheet::Role::Meta);

    inside->addSpacing(2);
    inside->addWidget(meta);

    // .mini's margin-top, and the row left-aligned in the column as a flex row is.
    row = new QWidget(panel);
    row->setObjectName("lastTrickCards");

    inside->addSpacing(16);
    inside->addWidget(row, 0, Qt::AlignLeft);

    closeButton = new SheetButton(QCoreApplication::translate("lasttrick", "Close"),
                                  SheetButton::Kind::Primary, panel);
    closeButton->setObjectName("closeLastTrick");
    closeButton->setDefault(true);

    addActions({ closeButton });

    connect(closeButton, &QPushButton::clicked, this, &LastTrickSheet::closeRequested);

    showTrick(std::nullopt, 0);
}

void LastTrickSheet::setCards(const CardRenderer* cards)
{
    renderer = cards;
    row->setFixedSize(rowSize());
    panel()->update();
}

void LastTrickSheet::showTrick(const std::optional<lasttrick::Trick>& trick, unsigned int currentRound)
{
    shown = trick;

    meta->setText(lasttrick::metaFor(trick, currentRound));

    row->setFixedSize(rowSize());
    row->setVisible(shown && !shown->cards.empty());

    panel()->update();
    placeSheet();
}

void LastTrickSheet::focusDefault()
{
    closeButton->setFocus(Qt::OtherFocusReason);
}

QRect LastTrickSheet::cardsArea() const
{
    return row->isVisible() ? row->geometry() : QRect();
}

QSize LastTrickSheet::rowSize() const
{
    const std::size_t count = shown ? shown->cards.size() : 0;

    if(count == 0)
        return QSize(0, 0);

    const QSizeF card = cardSizeOf(renderer);
    const QFontMetricsF names(sheet::fontFor(sheet::Role::Note));

    const qreal across = static_cast<qreal>(count) * card.width()
                       + static_cast<qreal>(count - 1) * lasttrick::cardGap;
    const qreal down = card.height() + lasttrick::nameAbove + names.lineSpacing();

    return QSize(static_cast<int>(std::ceil(across)), static_cast<int>(std::ceil(down)));
}

bool LastTrickSheet::eventFilter(QObject* watched, QEvent* event)
{
    // UNDER THE SHEET'S WIDGETS, which paint after their parent: the row's place
    // is an empty widget, so what is painted here shows through it, and the glow
    // round a card at the row's end is not cut off at the row's edge.
    if(watched == panel() && event->type() == QEvent::Paint && row->isVisible())
    {
        QPainter painter(panel());
        paintCards(painter);
    }

    return Overlay::eventFilter(watched, event);
}

void LastTrickSheet::paintCards(QPainter& painter) const
{
    if(!shown)
        return;

    const Theme::TablePalette& colours = theme().table;
    const QSizeF size = cardSizeOf(renderer);
    const QPointF origin = row->geometry().topLeft();
    const qreal ratio = painter.device()->devicePixelRatioF();

    const QFont font = sheet::fontFor(sheet::Role::Note);
    const QFontMetricsF metrics(font);

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    for(std::size_t i = 0 ; i < shown->cards.size() ; i++)
    {
        const lasttrick::Played& played = shown->cards[i];
        const QRectF card = lasttrick::cardRect(i, size).translated(origin);

        // .mini figure.win img's two box-shadows, the first on top: a ring 2 wide
        // round the card, over a glow. Both outside the card only, as CSS draws an
        // outer box-shadow.
        if(played.isWinning)
        {
            const QPainterPath shape = paths::rounded(card, lasttrick::cardRadius);

            shadows::drawShapeShadow(painter, shape, QPointF(), lasttrick::glowBlur,
                                     colours.sheetWinGlow);

            const QRectF ring = card.adjusted(-lasttrick::ringWidth, -lasttrick::ringWidth,
                                              lasttrick::ringWidth, lasttrick::ringWidth);

            painter.fillPath(paths::roundedBand(ring, lasttrick::cardRadius + lasttrick::ringWidth,
                                                card, lasttrick::cardRadius),
                             colours.goldBright);
        }

        // The face, rounded 6 as the mock-up's img is.
        if(renderer)
        {
            const QPixmap face = renderer->face(played.card, card.size().toSize(), ratio);

            painter.save();
            painter.setClipPath(paths::rounded(card, lasttrick::cardRadius));
            painter.drawPixmap(card, face, QRectF(face.rect()));
            painter.restore();
        }

        // The name under it, centred and given way to the card's width.
        const QRectF line(card.left(), card.bottom() + lasttrick::nameAbove, card.width(),
                          metrics.lineSpacing());
        const QString name = metrics.elidedText(played.name, Qt::ElideRight, card.width());

        painter.setFont(font);
        painter.setPen(colours.muted);
        painter.drawText(QPointF(line.left() + (line.width() - metrics.horizontalAdvance(name)) / 2.0,
                                 fonts::baselineIn(metrics, line.top(), line.height())),
                         name);
    }
}
