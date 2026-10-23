#include "CardArtWindow.h"

#include "paint/Avatars.h"
#include "table/SeatItem.h"
#include "table/TableSpace.h"

#include <romanian_whist/Card.h>

#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyleHints>
#include <QVBoxLayout>

#include <algorithm>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

namespace
{

// A black card for the sizes, so the rank pip is being judged at its hardest,
// and a RED one for the dimmed sample - section 18's desaturation is invisible
// on a card that was already black and white, which is exactly the case where
// checking it by eye would report a pass it had not earned.
const Card sizeSample(Rank::Ace, Suit::Spades);
const Card dimmedSample(Rank::Queen, Suit::Hearts);

constexpr qreal margin = 24.0;
constexpr qreal gap = 18.0;
constexpr qreal captionHeight = 20.0;

// How wide the row below is, in card heights: the three sizes (half, three
// quarters and a whole card), the back and the dimmed card at a whole card each -
// so 4.25 cards at an aspect of about 0.69 - and the portrait, 86 wide against a
// hand card 225 tall. Approximate on purpose, since the exact aspect is
// CardRenderer's business. All it decides is how large the samples are allowed
// to get.
constexpr qreal rowInCardHeights = 3.4;

// The gaps between the six samples, and the one extra before the back.
constexpr int gapsInRow = 6;

// The table's old limits on a card's height, which this window kept when the
// table stopped needing them: below 44 a rank pip stops being readable, and above
// 220 the samples stop looking like cards.
constexpr qreal minimumUnit = 44.0;
constexpr qreal maximumUnit = 220.0;

}

CardArtCanvas::CardArtCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(640, 320);
}

void CardArtCanvas::setTheme(const Theme& newTheme)
{
    theme = newTheme;
    update();
}

void CardArtCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal dpr = devicePixelRatioF();
    emit devicePixelRatioSeen(dpr);

    painter.drawPixmap(rect(), theme.background(size(), dpr));

    // THE UNIT, and every position below derived from it: the largest card the
    // row fits across the canvas, and no taller than half of it so the captions
    // have room beneath.
    //
    // THIS WINDOW'S OWN, where it used to be Theme's. Theme derived the TABLE'S
    // card height from the window, and this borrowed it; the table redesign's
    // phase 2 gave the table fixed card sizes in a space the view scales, so the
    // derivation went with it and what is left is a question only this canvas asks.
    const qreal across = (width() - 2.0 * margin - gapsInRow * gap) / rowInCardHeights;
    const qreal unit = std::clamp(std::min(height() * 0.5, across), minimumUnit, maximumUnit);

    painter.setPen(theme.textPrimary);

    qreal x = margin;

    // The row sits on a baseline down the middle of the canvas rather than
    // under the top edge, so the tallest sample has room above it and the
    // captions have room below at every window size.
    const qreal baseline = std::max(margin + unit, (height() + unit) / 2.0);

    // One card at three sizes. Same card, same renderer, three cache entries.
    const qreal heights[] = { unit * 0.5, unit * 0.75, unit };

    for(const qreal height : heights)
    {
        const QSize cardSize = cards.sizeForHeight(height);
        const QPixmap pixmap = cards.face(sizeSample, cardSize, dpr);

        painter.drawPixmap(QPointF(x, baseline - height), pixmap);
        painter.drawText(QRectF(x, baseline + 4.0, cardSize.width(), captionHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(qRound(height)));

        x += cardSize.width() + gap;
    }

    x += gap;

    const QSize full = cards.sizeForHeight(unit);

    painter.drawPixmap(QPointF(x, baseline - unit), cards.back(full, dpr));
    painter.drawText(QRectF(x, baseline + 4.0, full.width(), captionHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("Back"));
    x += full.width() + gap;

    painter.drawPixmap(QPointF(x, baseline - unit),
                       cards.face(dimmedSample, full, dpr, CardRenderer::State::Dimmed));
    painter.drawText(QRectF(x, baseline + 4.0, full.width(), captionHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("Dimmed"));
    x += full.width() + gap;

    // Beside the cards rather than on its own, because what matters about the
    // portrait is whether it reads at the size a seat's panel gives it: the
    // panel's 86 x 98, at the scale that makes a whole card here the table's hand
    // card.
    const QSize box = (seatitem::portraitPicture() * (unit / tablespace::handCardHeight)).toSize();

    painter.drawPixmap(QPointF(x, baseline - box.height()), theme.avatar(avatars::catalogue().empty() ? std::string() : avatars::catalogue().front().id, box, dpr));
    painter.drawText(QRectF(x, baseline + 4.0, box.width() * 2.0, captionHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("Avatar"));
}

CardArtWindow::CardArtWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Card art"));

    canvas = new CardArtCanvas(this);
    ratio = new QLabel(this);

    auto* scheme = new QComboBox(this);
    scheme->addItem(tr("Light"));
    scheme->addItem(tr("Dark"));

    // Opens on whichever the platform is actually using, so the first thing seen
    // is the palette the rest of the client will be looked at in.
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    scheme->setCurrentIndex(dark ? 1 : 0);
    canvas->setTheme(dark ? Theme::dark() : Theme::light());

    connect(scheme, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        canvas->setTheme(index == 1 ? Theme::dark() : Theme::light());
    });

    connect(canvas, &CardArtCanvas::devicePixelRatioSeen, this, [this](qreal dpr)
    {
        const QString text = tr("Device pixel ratio: %1").arg(dpr);
        if(ratio->text() != text)
            ratio->setText(text);
    });

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Palette:"), this));
    controls->addWidget(scheme);
    controls->addStretch();
    controls->addWidget(ratio);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(canvas, 1);

    resize(880, 420);
}
