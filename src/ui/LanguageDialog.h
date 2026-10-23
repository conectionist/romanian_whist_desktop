#ifndef LANGUAGE_DIALOG_H
#define LANGUAGE_DIALOG_H

#include "paint/Panel.h"
#include "paint/Theme.h"
#include "ui/LitButton.h"

#include <QDialog>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>

#include <optional>
#include <vector>

class QPainter;

// THE LANGUAGE THE CLIENT RUNS IN (the redesign's phase 13, the owner's call of
// 2026-10-23): chosen before the main window is built, so every string is made
// in it from the start and nothing has to be translated again while the app runs.
//
// TWO WAYS TO CHOOSE, and nothing remembered between runs yet (settings on disk
// are section 20's):
//
// - `--language en` or `--language ro` on the command line starts in that
//   language, without asking;
// - otherwise LanguageDialog asks, Romanian lit to start (the owner's call).
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

// "en" or "ro": what --language takes.
QString codeOf(Language language);

// The language's name in itself, which is how the dialog names it: "English",
// "Română".
QString nativeName(Language language);

// The compiled translation's resource, which qt_add_translations() embeds under
// :/i18n/; none for English, which is the source language.
QString translationResource(Language language);

// THE FLAG THE DIALOG SHOWS FOR `language`, filling `box`: Romania's tricolour for
// Romanian, and the Union Flag for English. Both at the box's shape - the dialog
// gives them one, 3:2 - so the two buttons match.
//
// THE COLOUR LITERALS OUTSIDE Theme, as the card back's are (CardRenderer::
// paintBack()): a flag is the same in every scheme, so a Theme colour would offer a
// choice that must not be made.
void paintFlag(QPainter& painter, const QRectF& box, Language language);

} // namespace language

// One of the dialog's two buttons: the language's flag, and nothing else on its
// face. Its text is the language's name, which a screen reader reads and the
// dialog paints under it.
class FlagButton : public LitButton
{
    Q_OBJECT

public:
    FlagButton(language::Language language, QWidget* parent = nullptr);

    language::Language language() const { return shown; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    language::Language shown;
};

// The room, the logo's one word - "Whist" is the game's name in both languages -
// and a flag per language, each named under its button, in the main-menu page's
// look. No words to translate: the flags and the names are the question.
//
// A QDialog run with exec() by main() before the main window exists: accepted with
// a choice, or rejected (closed, or Esc), and then the app quits.
class LanguageDialog : public QDialog
{
    Q_OBJECT

public:
    // Romanian has the keyboard, so Enter takes it.
    explicit LanguageDialog(QWidget* parent = nullptr);

    // What was chosen: Romanian until a button is pressed.
    language::Language choice() const { return chosen; }

    // Where each language's name is drawn, under its button: READ by the tests.
    QRectF nameRect(const FlagButton* button) const;

protected:
    // The room, the logo, the buttons' panels and their names.
    void paintEvent(QPaintEvent* event) override;

    // What lights a button lights the panel painted behind it, and its name.
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
    language::Language chosen = language::Language::Romanian;

    std::vector<FlagButton*> buttons;

    mutable QPixmap logoPicture;
    mutable panel::Cache chrome;
};

#endif
