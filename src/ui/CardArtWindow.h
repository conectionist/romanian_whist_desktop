#ifndef CARD_ART_WINDOW_H
#define CARD_ART_WINDOW_H

#include "paint/CardRenderer.h"
#include "paint/Theme.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// Everything B1 built, on screen at once: one card at three sizes, the back
// beside it, a dimmed face, an avatar, and all of it over Theme::background().
//
// This is the build order's "scratch widget drawing one card at three sizes",
// and it is KEPT rather than deleted once it has been looked at - the same call
// A5 made for the log, for the same reason. It is the only place the normal and
// dimmed renderings of one card are visible side by side, and it is what to open
// in B2, C and E on the day a card looks wrong on the table. Deleting it would
// mean writing it again from memory at exactly the moment something is already
// confusing.
//
// It reads the live devicePixelRatio back to the user rather than assuming one.
// "Check it on a HiDPI display now, not later" is a done-condition for this
// phase, and a number on screen is what makes that a check rather than a squint:
// with QT_SCALE_FACTOR=2 the label must say 2 before anything about the cards
// being crisp means anything at all.
class CardArtCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit CardArtCanvas(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

signals:
    // Emitted from paintEvent() with the ratio the cards were actually
    // rasterized at. Reported from there rather than read out of the screen on
    // demand because that is the number that matters: a label saying 2 while the
    // painter used 1 would be worse than no label at all.
    void devicePixelRatioSeen(qreal dpr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Theme theme = Theme::light();

    // By value, so its cache lives as long as the window and a repaint costs
    // nothing after the first. Also the reason this class is not const-correct
    // about it: CardRenderer's cache is mutable and its methods are const.
    CardRenderer cards;
};

class CardArtWindow : public QWidget
{
    Q_OBJECT

public:
    explicit CardArtWindow(QWidget* parent = nullptr);

private:
    CardArtCanvas* canvas;
    QLabel* ratio;
};

#endif
