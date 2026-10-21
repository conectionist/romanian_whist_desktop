#include <catch2/catch_test_macros.hpp>

#include "ui/NewGameDialog.h"
#include "ui/SettingsSheet.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStringList>

#include <vector>

// The menu bar's Settings (the redesign's phase 9): the bot speed and the round
// sheet's wait, announced as they change so the window can apply them live.
namespace
{
struct Announced
{
    int speed;
    bool wait;
};

}

TEST_CASE("Settings opens on the values it is given without announcing them", "[settings]")
{
    SettingsSheet sheet;

    std::vector<Announced> announced;
    QObject::connect(&sheet, &SettingsSheet::settingsChanged, [&announced](int speed, bool wait)
    {
        announced.push_back({ speed, wait });
    });

    sheet.setValues(1, true);

    QSlider* speed = sheet.findChild<QSlider*>(QStringLiteral("settingsSpeed"));
    QCheckBox* wait = sheet.findChild<QCheckBox*>(QStringLiteral("settingsWait"));
    REQUIRE(speed != nullptr);
    REQUIRE(wait != nullptr);

    REQUIRE(speed->value() == 1);
    REQUIRE(wait->isChecked());

    // Filling the sheet is not a change: nothing to apply to the game.
    REQUIRE(announced.empty());

    // The setup dialog's five positions.
    REQUIRE(speed->minimum() == newgame::SlowestSpeed);
    REQUIRE(speed->maximum() == newgame::FastestSpeed);
}

TEST_CASE("Each change to Settings is announced at once", "[settings]")
{
    SettingsSheet sheet;
    sheet.setValues(newgame::DefaultSpeed, false);

    std::vector<Announced> announced;
    QObject::connect(&sheet, &SettingsSheet::settingsChanged, [&announced](int speed, bool wait)
    {
        announced.push_back({ speed, wait });
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

    // Done only closes: everything is already applied, so there is nothing to
    // confirm and nothing to cancel.
    sheet.findChild<QPushButton*>(QStringLiteral("settingsDone"))->click();
    REQUIRE(closed == 1);
    REQUIRE(announced.size() == 2);
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
