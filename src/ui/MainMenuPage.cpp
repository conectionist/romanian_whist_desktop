#include "MainMenuPage.h"

#include <QBoxLayout>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPushButton>

MainMenuPage::MainMenuPage(QWidget* parent) : QWidget(parent)
{
    // THE TITLE AND THE BUTTONS SIT ON A PLAQUE, and not straight on the green.
    // Theme has no colour for text on the table surface, because until D1
    // nothing wrote any: every word on the table is on a seat plaque, and
    // textPrimary is chosen against seatPlaque rather than against tableSurface.
    //
    // Found by rendering it. The title drawn in textPrimary directly on the
    // background was dark grey on dark green and barely legible - which is the
    // same defect C4 recorded when the round-type label was painted in `accent`
    // and measured 2.0:1. Putting the panel behind the words uses the pair of
    // colours that were picked for each other, rather than adding a third.
    panel = new QWidget(this);
    panel->setAutoFillBackground(true);

    QVBoxLayout* inside = new QVBoxLayout(panel);
    inside->setContentsMargins(48, 36, 48, 36);

    QLabel* title = new QLabel(tr("Romanian Whist"), panel);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("title");

    // OFF THE WIDGET'S OWN FONT rather than a point size written here. A window
    // on a HiDPI display or with a large system font gets a title that grew with
    // everything else, which is the same rule the panels follow - B2b's finding
    // was a column measured in one font and drawn in another, and a hardcoded
    // size is that mistake with the measurement left out entirely.
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.2);
    titleFont.setBold(true);
    title->setFont(titleFont);

    inside->addWidget(title);
    inside->addSpacing(24);

    newGame = new QPushButton(tr("New Game"), panel);
    newGame->setObjectName("newGame");
    newGame->setDefault(true);

    quit = new QPushButton(tr("Quit"), panel);
    quit->setObjectName("quit");

    // A FIXED COLUMN, not buttons stretched to the window. The page is as wide
    // as the table it replaces, and a New Game button a metre across reads as a
    // banner rather than as something to press.
    for(QPushButton* button : { newGame, quit })
    {
        button->setMinimumWidth(220);
        button->setMinimumHeight(36);
        inside->addWidget(button);
    }

    // The panel centred both ways, and no bigger than its contents.
    QVBoxLayout* stack = new QVBoxLayout(this);
    stack->addStretch(1);

    QHBoxLayout* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(panel);
    row->addStretch(1);

    stack->addLayout(row);
    stack->addStretch(1);

    connect(newGame, &QPushButton::clicked, this, &MainMenuPage::newGameRequested);
    connect(quit, &QPushButton::clicked, this, &MainMenuPage::quitRequested);
}

void MainMenuPage::setTheme(const Theme& _theme)
{
    theme = _theme;

    // Through the palette rather than a stylesheet, the call InfoStrip, BidStrip
    // and PromptBar all make - section 18.1's rule is that colours come from
    // Theme, and a stylesheet string is a colour constructed somewhere else.
    QPalette scheme = panel->palette();
    scheme.setColor(QPalette::Window, theme.seatPlaque);
    scheme.setColor(QPalette::WindowText, theme.textPrimary);
    panel->setPalette(scheme);

    update();
}

void MainMenuPage::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // THE SAME BACKGROUND THE TABLE DRAWS, through Theme's own function rather
    // than a flat fill of tableSurface - so the menu and the game are one
    // application, and the day background() is pointed at real art the menu gets
    // it too without being touched.
    painter.drawPixmap(0, 0, theme.background(size(), devicePixelRatioF()));

    QWidget::paintEvent(event);
}
