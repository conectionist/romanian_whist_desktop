#include <catch2/catch_test_macros.hpp>

#include "table/Motion.h"
#include "ui/NewGameDialog.h"
#include "ui/SettingsSheet.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QCoreApplication>
#include <QWidget>

#include <vector>

// The menu bar's Settings (the redesign's phase 9): the bot speed and the round
// sheet's wait, announced as they change so the window can apply them live - and,
// since phase 12, the cards' animations.
namespace
{
struct Announced
{
    int speed;
    bool wait;
    motion::Pace animations;
};

}

TEST_CASE("Settings opens on the values it is given without announcing them", "[settings]")
{
    SettingsSheet sheet;

    std::vector<Announced> announced;
    QObject::connect(&sheet, &SettingsSheet::settingsChanged,
                     [&announced](int speed, bool wait, motion::Pace animations)
    {
        announced.push_back({ speed, wait, animations });
    });

    sheet.setValues(1, true, motion::Pace::Quick);

    QSlider* speed = sheet.findChild<QSlider*>(QStringLiteral("settingsSpeed"));
    QCheckBox* wait = sheet.findChild<QCheckBox*>(QStringLiteral("settingsWait"));
    REQUIRE(speed != nullptr);
    REQUIRE(wait != nullptr);

    REQUIRE(speed->value() == 1);
    REQUIRE(wait->isChecked());

    // THE ANIMATIONS' SLIDER: four stops, Off to Slow, on the pace it was given.
    QSlider* animations = sheet.findChild<QSlider*>(QStringLiteral("settingsAnimations"));
    REQUIRE(animations != nullptr);
    REQUIRE(animations->minimum() == 0);
    REQUIRE(animations->maximum() == motion::stopCount - 1);
    REQUIRE(animations->value() == motion::stopOf(motion::Pace::Quick));

    // Filling the sheet is not a change: nothing to apply to the game.
    REQUIRE(announced.empty());

    // The setup dialog's five positions.
    REQUIRE(speed->minimum() == newgame::SlowestSpeed);
    REQUIRE(speed->maximum() == newgame::FastestSpeed);
}

TEST_CASE("Each change to Settings is announced at once", "[settings]")
{
    SettingsSheet sheet;
    sheet.setValues(newgame::DefaultSpeed, false, motion::Pace::Normal);

    std::vector<Announced> announced;
    QObject::connect(&sheet, &SettingsSheet::settingsChanged,
                     [&announced](int speed, bool wait, motion::Pace animations)
    {
        announced.push_back({ speed, wait, animations });
    });

    int closed = 0;
    QObject::connect(&sheet, &SettingsSheet::closeRequested, [&closed]{ closed++; });

    sheet.findChild<QSlider*>(QStringLiteral("settingsSpeed"))->setValue(newgame::FastestSpeed);

    REQUIRE(announced.size() == 1);
    REQUIRE(announced.back().speed == newgame::FastestSpeed);
    REQUIRE_FALSE(announced.back().wait);

    sheet.findChild<QCheckBox*>(QStringLiteral("settingsWait"))->setChecked(true);

    REQUIRE(announced.size() == 2);
    REQUIRE(announced.back().speed == newgame::FastestSpeed);
    REQUIRE(announced.back().wait);

    // AND THE ANIMATIONS' SLIDER, announced as the pace its stop is, the other two
    // settings carried along as they stand.
    sheet.findChild<QSlider*>(QStringLiteral("settingsAnimations"))->setValue(motion::stopOf(motion::Pace::Off));

    REQUIRE(announced.size() == 3);
    REQUIRE(announced.back().animations == motion::Pace::Off);
    REQUIRE(announced.back().speed == newgame::FastestSpeed);
    REQUIRE(announced.back().wait);

    // Done only closes: everything is already applied, so there is nothing to
    // confirm and nothing to cancel.
    sheet.findChild<QPushButton*>(QStringLiteral("settingsDone"))->click();
    REQUIRE(closed == 1);
    REQUIRE(announced.size() == 3);
}

TEST_CASE("Settings and the setup dialog build the same pace controls", "[settings]")
{
    // PR #35's review, finding #12: the two were copies, free to drift apart.
    // Both now come from newgame::buildSpeedRow() and buildWaitCheckBox().
    SettingsSheet sheet;
    NewGameDialog dialog(newgame::defaults());

    QSlider* sheetSpeed = sheet.findChild<QSlider*>(QStringLiteral("settingsSpeed"));
    QSlider* dialogSpeed = dialog.findChild<QSlider*>(QStringLiteral("speed"));
    REQUIRE(sheetSpeed != nullptr);
    REQUIRE(dialogSpeed != nullptr);

    REQUIRE(sheetSpeed->minimum() == dialogSpeed->minimum());
    REQUIRE(sheetSpeed->maximum() == dialogSpeed->maximum());
    REQUIRE(sheetSpeed->pageStep() == dialogSpeed->pageStep());
    REQUIRE(sheetSpeed->tickPosition() == dialogSpeed->tickPosition());

    // The words either side of the slider, in both.
    const auto labels = [](const QWidget& owner)
    {
        QStringList texts;

        for(const QLabel* label : owner.findChildren<QLabel*>())
            texts << label->text();

        return texts;
    };

    for(const QString& word : { QStringLiteral("Bot speed"), QStringLiteral("Slow"), QStringLiteral("Instant") })
    {
        INFO(word.toStdString());
        REQUIRE(labels(sheet).contains(word));
        REQUIRE(labels(dialog).contains(word));
    }

    QCheckBox* sheetWait = sheet.findChild<QCheckBox*>(QStringLiteral("settingsWait"));
    QCheckBox* dialogWait = dialog.findChild<QCheckBox*>(QStringLiteral("waitEachRound"));
    REQUIRE(sheetWait != nullptr);
    REQUIRE(dialogWait != nullptr);
    REQUIRE(sheetWait->text() == dialogWait->text());
}

// THE TWO SLIDERS LINE UP: "Bot speed" and "Card animations" are not the same
// length, nor are Slow and Off, and a slider that began where its words ended would
// leave the two rows ragged.
TEST_CASE("The two sliders start and end together", "[settings]")
{
    QWidget page;
    page.resize(1000, 700);

    SettingsSheet sheet(&page);
    sheet.setValues(newgame::DefaultSpeed, false, motion::Pace::Normal);

    page.show();
    sheet.show();
    QCoreApplication::processEvents();

    QSlider* speed = sheet.findChild<QSlider*>(QStringLiteral("settingsSpeed"));
    QSlider* animations = sheet.findChild<QSlider*>(QStringLiteral("settingsAnimations"));
    REQUIRE(speed != nullptr);
    REQUIRE(animations != nullptr);

    REQUIRE(speed->mapTo(&sheet, QPoint(0, 0)).x() == animations->mapTo(&sheet, QPoint(0, 0)).x());
    REQUIRE(speed->width() == animations->width());

    // One above the other, the animations' second.
    REQUIRE(animations->mapTo(&sheet, QPoint(0, 0)).y() > speed->mapTo(&sheet, QPoint(0, 0)).y());
}
