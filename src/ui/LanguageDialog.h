#ifndef LANGUAGE_DIALOG_H
#define LANGUAGE_DIALOG_H

#include "paint/Panel.h"
#include "paint/Theme.h"

#include <QDialog>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class MenuPageButton;

// THE LANGUAGE THE CLIENT RUNS IN (the redesign's phase 13, the owner's call of
// 2026-10-23): chosen before the main window is built, so every string is made
// in it from the start and nothing has to be translated again while the app runs.
//
// TWO WAYS TO CHOOSE, and nothing remembered between runs yet (settings on disk
// are section 20's):
//
// - `--language en` or `--language ro` on the command line starts in that
//   language, without asking;
// - otherwise LanguageDialog asks, with the system's language picked to start.
namespace language
{

enum class Language
{
    English,
    Romanian,
};

// A code as the command line gives it: "en" or "ro", in any case, with or without
// a country ("ro_RO", "en-GB"). Anything else is none.
std::optional<Language> fromCode(const QString& code);

// The first of the system's languages (QLocale::system().uiLanguages()) that the
// client speaks; English when it speaks none of them.
Language fromSystem(const QStringList& uiLanguages);

// "en" or "ro": what --language takes.
QString codeOf(Language language);

// The language's name in itself, which is how a button offers it: "English",
// "Română".
QString nativeName(Language language);

// The compiled translation's resource, which qt_add_translations() embeds under
// :/i18n/; none for English, which is the source language.
QString translationResource(Language language);

} // namespace language

// "Choose your language · Alege limba": the room, the round logo's word - "Whist"
// is the game's name in both languages - and a button per language in the main-
// menu page's look. Its words are in both languages at once, so none of them goes
// through translation.
//
// A QDialog run with exec() by main() before the main window exists: accepted with
// a choice, or rejected (closed, or Esc), and then the app quits.
class LanguageDialog : public QDialog
{
    Q_OBJECT

public:
    // `preselected` has the keyboard, so Enter takes it.
    explicit LanguageDialog(language::Language preselected, QWidget* parent = nullptr);

    // What was chosen: `preselected` until a button is pressed.
    language::Language choice() const { return chosen; }

protected:
    // The room, the logo, the two lines of words, and the buttons' panels.
    void paintEvent(QPaintEvent* event) override;

    // What lights a button lights the panel painted behind it.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // THE BUTTON WITH THE KEYBOARD LOOKS IT whenever the dialog is the active window,
    // so Enter's answer is in plain sight. A button lights for the keyboard only when
    // Tab brought it there (LitButton::isLit()), and a window becoming active hands
    // the keyboard over for another reason - so it is handed over again, as Tab would,
    // once Qt's own handover is done.
    bool event(QEvent* event) override;

private:
    const QPixmap& logo(qreal ratio) const;

    Theme theme = Theme::dark();
    language::Language chosen;

    std::vector<MenuPageButton*> buttons;

    mutable QPixmap logoPicture;
    mutable panel::Cache chrome;
};

#endif
