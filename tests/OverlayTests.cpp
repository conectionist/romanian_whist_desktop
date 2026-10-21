#include <catch2/catch_test_macros.hpp>

#include "paint/Theme.h"
#include "ui/Overlay.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPushButton>

#include <cstdlib>

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

}

TEST_CASE("An overlay dims what is behind it in the theme's colour", "[overlay]")
{
    QWidget page;
    page.resize(400, 300);

    TestSheet sheet(&page);
    sheet.setGeometry(page.rect());
    sheet.setTheme(Theme::dark());

    // Only the sheet's own pixels: without the empty flags, render() lays the
    // window's background down first.
    QImage image(sheet.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    sheet.render(&image, QPoint(), QRegion(), QWidget::RenderFlags());

    // A corner, well away from the plaque in the middle.
    const QColor corner = image.pixelColor(2, 2);
    const QColor dim = Theme::dark().dimmedOverlay;

    REQUIRE(corner.alpha() == dim.alpha());
    REQUIRE(std::abs(corner.red() - dim.red()) <= 1);
    REQUIRE(std::abs(corner.green() - dim.green()) <= 1);
    REQUIRE(std::abs(corner.blue() - dim.blue()) <= 1);
}

TEST_CASE("An overlay's plaque takes the theme's colours", "[overlay]")
{
    QWidget page;
    TestSheet sheet(&page);

    sheet.setTheme(Theme::dark());

    REQUIRE(sheet.plaque()->palette().color(QPalette::Window) == Theme::dark().seatPlaque);
    REQUIRE(sheet.plaque()->palette().color(QPalette::WindowText) == Theme::dark().textPrimary);
    REQUIRE(sheet.plaque()->autoFillBackground());
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
