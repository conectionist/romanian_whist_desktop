#include <catch2/catch_test_macros.hpp>

#include "paint/Theme.h"
#include "ui/GameOverPanel.h"
#include "ui/Overlay.h"
#include "ui/PauseOverlay.h"
#include "ui/RoundSheet.h"
#include "ui/RulesSheet.h"
#include "ui/SettingsSheet.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// What every sheet over the page shares (PR #35's review, finding #11): the pause
// menu, the round sheet, the game-over panel, Rules and Settings.
namespace
{
// The smallest sheet: one button on the plaque.
class TestSheet : public Overlay
{
public:
    explicit TestSheet(QWidget* parent) : Overlay(parent)
    {
        button = new QPushButton(QStringLiteral("OK"), panel());
        panelLayout()->addWidget(button);
    }

    void focusDefault() override { button->setFocus(Qt::OtherFocusReason); }

    QWidget* plaque() const { return panel(); }

    QPushButton* button = nullptr;
};

// Every colour channel within `tolerance` of the other's.
bool near(const QColor& a, const QColor& b, int tolerance)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
        && std::abs(a.blue() - b.blue()) <= tolerance && std::abs(a.alpha() - b.alpha()) <= tolerance;
}

// What a widget paints itself, and nothing behind it: without the empty flags,
// render() lays the window's background down first.
QImage ownPixels(QWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image, QPoint(), QRegion(), QWidget::RenderFlags());
    return image;
}

// The five sheets, filled as they are filled in a game, each on a page of its own
// of `size`: every one the window raises.
struct FiveSheets
{
    std::vector<std::unique_ptr<QWidget>> pages;
    std::vector<Overlay*> sheets;

    explicit FiveSheets(QSize size)
    {
        const auto page = [this, size]
        {
            pages.push_back(std::make_unique<QWidget>());
            pages.back()->resize(size);
            return pages.back().get();
        };

        sheets.push_back(new PauseOverlay(page()));

        RoundSheet* round = new RoundSheet(page());
        RoundResult result;
        result.roundNumber = 7;
        result.roundCount = 26;
        result.trickCount = 6;
        result.hasTrump = true;
        result.trump = romanian_whist::Card(romanian_whist::Rank::King, romanian_whist::Suit::Hearts);
        result.nextRoundCountsForStreaks = true;

        for(const char* name : { "Ana", "Bogdan", "Cristina", "Dan", "Elena", "Player" })
        {
            RoundSeatResult seat;
            seat.name = name;
            seat.hasBet = true;
            seat.bet = 2;
            seat.tricksWon = 1;
            seat.roundScore = -1;
            seat.totalScore = 30;
            seat.lossStreak = 4;
            result.seats.push_back(seat);
        }

        round->showResult(result);
        sheets.push_back(round);

        GameOverPanel* over = new GameOverPanel(page());
        std::vector<romanian_whist::Standing> standings;

        for(unsigned int i = 0 ; i < 6 ; i++)
            standings.push_back({ romanian_whist::Seat{ i }, "Seat " + std::to_string(i), 40 - static_cast<int>(i), i + 1 });

        over->showResult(standings, { standings.front() });
        sheets.push_back(over);

        RulesSheet* rules = new RulesSheet(page());
        newgame::Config config = newgame::defaults();
        config.all1GamesAreForehead = true;
        config.endWithForeheadAndHidden = true;
        rules->showRules(config);
        sheets.push_back(rules);

        sheets.push_back(new SettingsSheet(page()));

        for(Overlay* sheet : sheets)
        {
            sheet->parentWidget()->show();
            sheet->show();
        }

        QCoreApplication::processEvents();
    }
};

}

TEST_CASE("An overlay dims what is behind it in the theme's colour", "[overlay]")
{
    QWidget page;
    page.resize(400, 300);

    TestSheet sheet(&page);
    sheet.setGeometry(page.rect());
    sheet.setTheme(Theme::dark());

    const QImage image = ownPixels(sheet);

    // A corner, well away from the sheet in the middle: section 2.13's scrim.
    REQUIRE(near(image.pixelColor(2, 2), Theme::dark().table.scrim, 1));
}

// SECTION 2.13's PANEL, the table's own (section 2.2), round the sheet - not a
// widget's background: the plaque the words sit on is transparent, and the gold
// border and dark gradient are painted round it.
TEST_CASE("A sheet is drawn in the table's panel and its words in cream", "[overlay]")
{
    QWidget page;
    page.resize(900, 600);

    TestSheet sheet(&page);
    sheet.setTheme(Theme::dark());
    page.show();
    sheet.show();
    QCoreApplication::processEvents();

    REQUIRE_FALSE(sheet.plaque()->autoFillBackground());
    REQUIRE(sheet.plaque()->palette().color(QPalette::WindowText) == Theme::dark().table.cream);

    const QImage image = ownPixels(sheet);
    const QRect frame = sheet.sheetRect();

    // The border, gold, along the middle of the sheet's top edge; and just inside
    // the inset lines, the panel's gradient over the scrim, much darker than it.
    const QColor border = image.pixelColor(frame.center().x(), frame.top());
    const QColor inside = image.pixelColor(frame.center().x(), frame.top() + 8);

    REQUIRE(near(border, Theme::dark().table.panelBorder, 12));
    REQUIRE(inside.lightness() < 40);
    REQUIRE(inside != image.pixelColor(2, 2));
}

TEST_CASE("A sheet is section 2.13's 720 wide and centred on the page", "[overlay]")
{
    QWidget page;
    page.resize(1920, 1034);

    TestSheet sheet(&page);
    page.show();
    sheet.show();
    QCoreApplication::processEvents();

    const QRect frame = sheet.sheetRect();

    REQUIRE(frame.width() == sheet::width);
    REQUIRE(std::abs(frame.center().x() - page.rect().center().x()) <= 1);
    REQUIRE(std::abs(frame.center().y() - page.rect().center().y()) <= 1);
}

// IN A WINDOW TOO SMALL FOR IT, a sheet narrows to the page less its margins, and
// what does not fit scrolls, as the mock-up's .sheet does past its max-height. The
// smallest page is the table's minimum viewport, 709 x 495.
TEST_CASE("Every sheet stays on the page at the smallest window and Rules scrolls there", "[overlay]")
{
    FiveSheets five(QSize(709, 495));

    for(Overlay* sheet : five.sheets)
    {
        INFO(sheet->metaObject()->className());

        const QRect page = sheet->parentWidget()->rect().adjusted(sheet::pageMargin, sheet::pageMargin,
                                                                  -sheet::pageMargin, -sheet::pageMargin);

        REQUIRE(page.contains(sheet->sheetRect()));
    }

    // The longest of them, the rules of a game that deals both special rounds,
    // scrolls rather than leaving the page.
    QScrollArea* scroll = five.sheets[3]->findChild<QScrollArea*>();
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->verticalScrollBar()->maximum() > 0);

    // And at the screenshot's size, none does.
    FiveSheets roomy(QSize(1920, 1034));

    for(Overlay* sheet : roomy.sheets)
    {
        INFO(sheet->metaObject()->className());
        REQUIRE(sheet->findChild<QScrollArea*>()->verticalScrollBar()->maximum() == 0);
        REQUIRE(sheet->sheetRect().width() == sheet::width);
    }
}

// THE WINDOW'S ORDER, NOT A TEST'S: MainWindow builds Rules at start-up, the event
// loop lays it out with no rules in it, and the rules arrive when the bar's Rules is
// first pressed. The list sits in a row of its own, which kept the height it had
// measured empty - one line - so in the app the sheet opened as a sliver with a
// scroll bar (found in the running app, phase 10).
TEST_CASE("A sheet filled after it was laid out is sized to what it then holds", "[overlay]")
{
    QWidget page;
    page.resize(1280, 754);

    // Hidden from the first, as the window keeps it until Rules is pressed.
    RulesSheet sheet(&page);
    sheet.hide();
    page.show();
    QCoreApplication::processEvents();

    newgame::Config config = newgame::defaults();
    config.endWithForeheadAndHidden = true;
    sheet.showRules(config);

    sheet.show();
    QCoreApplication::processEvents();

    QScrollArea* scroll = sheet.findChild<QScrollArea*>();
    REQUIRE(scroll != nullptr);

    // The whole list fits a page this tall, so nothing scrolls, and the sheet is
    // far taller than a title, one line and a button.
    REQUIRE(scroll->verticalScrollBar()->maximum() == 0);
    REQUIRE(sheet.sheetRect().height() > 300);
}

TEST_CASE("A sheet button lights under the cursor and when Tab brings the keyboard", "[overlay]")
{
    QWidget page;
    page.resize(400, 200);

    // Away from the page's corner, where the offscreen platform keeps its cursor.
    SheetButton* button = new SheetButton(QStringLiteral("Resume"), SheetButton::Kind::Primary, &page);
    button->move(150, 60);

    QPushButton* other = new QPushButton(QStringLiteral("Other"), &page);
    other->move(150, 140);

    page.show();
    QCoreApplication::processEvents();

    REQUIRE_FALSE(button->underMouse());
    REQUIRE_FALSE(button->isLit());

    // THE FOCUS A SHEET GIVES ITS DEFAULT BUTTON IS NOT A RING: a browser draws no
    // :focus-visible for focus the page moved there itself.
    button->setFocus(Qt::OtherFocusReason);
    REQUIRE(page.focusWidget() == button);
    REQUIRE_FALSE(button->isLit());

    // Brought there by Tab, it is.
    other->setFocus(Qt::OtherFocusReason);
    button->setFocus(Qt::TabFocusReason);
    REQUIRE(button->isLit());

    // And it goes out with the keyboard.
    other->setFocus(Qt::TabFocusReason);
    REQUIRE_FALSE(button->isLit());

    // Under the cursor: what Qt marks a widget the pointer is over, which a sent
    // enter event does not set.
    button->setAttribute(Qt::WA_UnderMouse, true);
    REQUIRE(button->isLit());

    button->setAttribute(Qt::WA_UnderMouse, false);
    REQUIRE_FALSE(button->isLit());
}

TEST_CASE("Tab lights a sheet's only button though the keyboard is already on it", "[overlay]")
{
    // Rules has one button, Close, which the sheet puts the keyboard on as it opens -
    // unlit, as a page's own focus is. Tab finds nowhere else to go; it must still
    // show where the keyboard is.
    QWidget page;
    page.resize(1280, 754);

    RulesSheet sheet(&page);
    sheet.showRules(newgame::defaults());
    page.show();
    sheet.show();
    QCoreApplication::processEvents();

    sheet.focusDefault();

    SheetButton* close = sheet.findChild<SheetButton*>(QStringLiteral("rulesClose"));
    REQUIRE(close != nullptr);
    REQUIRE(page.focusWidget() == close);
    REQUIRE_FALSE(close->isLit());

    REQUIRE(sheet.focusNextInside(true));
    REQUIRE(page.focusWidget() == close);
    REQUIRE(close->isLit());
}

TEST_CASE("A quiet sheet button has no fill and a primary one is green", "[overlay]")
{
    SheetButton quiet(QStringLiteral("Main menu"), SheetButton::Kind::Quiet);
    SheetButton primary(QStringLiteral("Main menu"), SheetButton::Kind::Primary);

    quiet.resize(quiet.sizeHint());
    primary.resize(primary.sizeHint());

    REQUIRE(quiet.sizeHint() == primary.sizeHint());

    const QImage quietPixels = ownPixels(quiet);
    const QImage primaryPixels = ownPixels(primary);

    // Inside the border, clear of the label: under the top border, a little in.
    const QPoint at(6, 3);

    REQUIRE(quietPixels.pixelColor(at).alpha() == 0);

    const QColor green = primaryPixels.pixelColor(at);
    REQUIRE(green.alpha() == 255);
    REQUIRE(green.green() > green.red());
    REQUIRE(green.green() > green.blue());
}

TEST_CASE("A sheet table rules its headings and every row", "[overlay]")
{
    SheetTable table;
    table.setColumns({ { QStringLiteral("Player"), Qt::AlignLeft }, { QStringLiteral("Total") } });
    table.addRow({ { QStringLiteral("Ana") }, { QStringLiteral("41") } });
    table.addRow({ { QStringLiteral("Bogdan") }, { QStringLiteral("33") } });

    table.resize(400, table.sizeHint().height());
    table.show();
    QCoreApplication::processEvents();

    REQUIRE(table.rowCount() == 2);
    REQUIRE(table.cell(1, 0)->text() == QStringLiteral("Bogdan"));
    REQUIRE(table.cell(2, 0) == nullptr);

    const QImage image = ownPixels(table);
    const Theme::TablePalette palette = Theme::dark().table;

    // Down the table's right-hand edge, clear of every word: one rule under the
    // headings and one under each row, the headings' the brighter.
    std::vector<int> ruled;

    for(int y = 0 ; y < image.height() ; y++)
    {
        if(image.pixelColor(image.width() - 1, y).alpha() > 0)
            ruled.push_back(y);
    }

    REQUIRE(ruled.size() == 3);

    // Read back from a premultiplied image, a colour at a tenth of full alpha is
    // only good to a few levels in each channel; the alpha says which rule it is.
    const QColor head = image.pixelColor(image.width() - 1, ruled[0]);
    const QColor row = image.pixelColor(image.width() - 1, ruled[1]);

    INFO("head " << head.name(QColor::HexArgb).toStdString() << ", row " << row.name(QColor::HexArgb).toStdString());
    REQUIRE(std::abs(head.alpha() - palette.sheetHeadRule.alpha()) <= 1);
    REQUIRE(std::abs(row.alpha() - palette.sheetRowRule.alpha()) <= 1);
    REQUIRE(near(head, palette.sheetHeadRule, 4));
    REQUIRE(near(row, palette.sheetRowRule, 12));
    REQUIRE(ruled.back() == image.height() - 1);

    // Cleared, the headings stay and the rows go.
    table.clearRows();
    REQUIRE(table.rowCount() == 0);
    REQUIRE(table.cell(0, 0) == nullptr);
}

TEST_CASE("The keyboard scrolls a sheet whose body does not fit", "[overlay]")
{
    // PR #36's review, finding #7, which this showed to be a false alarm. The body
    // scrolls in a short window, and its scroll area is never a Tab stop, so the
    // keyboard is always on one of the sheet's buttons. A button takes neither Page
    // Up nor Home, so Qt passes them up to its parents, and the buttons sit inside
    // the scrolled body: the scroll area takes them. Pinned, because a sheet whose
    // actions moved out of the scroll area - a footer that stays put, say - would
    // put the top of the rules out of the keyboard's reach once a Tab to Close had
    // scrolled to the bottom.
    FiveSheets five(QSize(709, 495));
    Overlay* rules = five.sheets[3];

    QScrollArea* scroll = rules->findChild<QScrollArea*>();
    REQUIRE(scroll != nullptr);

    QScrollBar* bar = scroll->verticalScrollBar();
    REQUIRE(bar->maximum() > 0);

    // As a Tab to it leaves things (Overlay::focusNextInside()).
    QPushButton* close = rules->findChild<QPushButton*>("rulesClose");
    REQUIRE(close != nullptr);

    close->setFocus(Qt::TabFocusReason);
    scroll->ensureWidgetVisible(close);
    REQUIRE(bar->value() == bar->maximum());

    // To the button, which passes on what it does not take.
    const auto press = [close](int key)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(close, &event);
    };

    press(Qt::Key_PageUp);
    REQUIRE(bar->value() < bar->maximum());

    press(Qt::Key_Home);
    REQUIRE(bar->value() == bar->minimum());

    press(Qt::Key_PageDown);
    REQUIRE(bar->value() > bar->minimum());

    press(Qt::Key_End);
    REQUIRE(bar->value() == bar->maximum());
}

TEST_CASE("A sheet table shows a name as typed, markup and all", "[overlay]")
{
    // PR #36's review, finding #5. A QLabel's default AutoText reads a string that
    // looks like markup as rich text, so a player who named themselves <b>Dan</b>
    // in the setup dialog showed in bold - or <img src=x>, as a broken picture.
    SheetTable table;
    table.setColumns({ { QStringLiteral("Player"), Qt::AlignLeft }, { QStringLiteral("Total") } });
    table.addRow({ { QStringLiteral("<b>Dan</b>") }, { QStringLiteral("41") } });

    REQUIRE(table.cell(0, 0)->textFormat() == Qt::PlainText);
    REQUIRE(table.cell(0, 0)->text() == QStringLiteral("<b>Dan</b>"));
}

TEST_CASE("A press on an overlay goes no further", "[overlay]")
{
    QWidget page;
    page.resize(400, 300);

    TestSheet sheet(&page);
    sheet.setGeometry(page.rect());

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5.0, 5.0), QPointF(5.0, 5.0),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press.ignore();

    QCoreApplication::sendEvent(&sheet, &press);

    REQUIRE(press.isAccepted());
}

TEST_CASE("An overlay covers its parent at every size", "[overlay]")
{
    // PR #35's review, finding #2. The parent is the window's page stack, which
    // changes size with the window and also when the log dock comes and goes. A
    // sheet sized only on the window's resizes kept its old size: too tall and
    // off-centre, or short of the bottom - uncovering the hand, and leaving it
    // clickable, under a sheet that was holding the game.
    QWidget window;
    window.resize(800, 600);
    window.show();

    // A child rather than the window itself, so its resizes arrive at once.
    QWidget* page = new QWidget(&window);
    page->setGeometry(0, 0, 400, 300);
    page->show();

    TestSheet sheet(page);
    sheet.show();
    REQUIRE(sheet.geometry() == page->rect());

    page->resize(640, 480);
    REQUIRE(sheet.geometry() == page->rect());

    page->resize(500, 260);
    REQUIRE(sheet.geometry() == page->rect());

    // And while hidden, so that it opens at the page's size.
    sheet.hide();
    page->resize(720, 540);
    sheet.show();
    REQUIRE(sheet.geometry() == page->rect());
}

TEST_CASE("Tab inside an overlay wraps round and skips what it cannot stop on", "[overlay]")
{
    // PR #35's review, finding #4: Tab must not leave a sheet for what is under
    // its dim. The window hands Tab to focusNextInside() for the sheet on top.
    QWidget window;
    window.resize(400, 300);

    // Something outside the sheet, where the keyboard starts.
    QPushButton* outside = new QPushButton(QStringLiteral("Outside"), &window);

    TestSheet sheet(&window);
    QPushButton* second = new QPushButton(QStringLiteral("Second"), sheet.plaque());
    QPushButton* off = new QPushButton(QStringLiteral("Disabled"), sheet.plaque());
    QPushButton* gone = new QPushButton(QStringLiteral("Hidden"), sheet.plaque());
    QPushButton* third = new QPushButton(QStringLiteral("Third"), sheet.plaque());

    for(QPushButton* button : { second, off, gone, third })
        sheet.plaque()->layout()->addWidget(button);

    off->setEnabled(false);

    window.show();
    gone->hide();

    outside->setFocus();
    REQUIRE(window.focusWidget() == outside);

    // From outside, forward lands on the first stop.
    REQUIRE(sheet.focusNextInside(true));
    REQUIRE(window.focusWidget() == sheet.button);

    REQUIRE(sheet.focusNextInside(true));
    REQUIRE(window.focusWidget() == second);

    // The disabled and the hidden button are passed over.
    REQUIRE(sheet.focusNextInside(true));
    REQUIRE(window.focusWidget() == third);

    // And round again, both ways.
    REQUIRE(sheet.focusNextInside(true));
    REQUIRE(window.focusWidget() == sheet.button);

    REQUIRE(sheet.focusNextInside(false));
    REQUIRE(window.focusWidget() == third);

    // From outside, backward lands on the last.
    outside->setFocus();
    REQUIRE(sheet.focusNextInside(false));
    REQUIRE(window.focusWidget() == third);
}
