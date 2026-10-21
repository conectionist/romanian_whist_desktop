#ifndef LIT_BUTTON_H
#define LIT_BUTTON_H

#include <QPushButton>
#include <QString>

// A QPushButton that knows when it looks lit, as the mock-up's buttons do under
// :hover and :focus-visible: under the cursor, held down, or with the keyboard
// brought to it by Tab. NOT merely focused: a sheet or page puts the keyboard on
// its default button when it opens (Overlay::focusDefault(),
// MainMenuPage::focusNewGame()), and a browser draws no focus ring for focus a page
// moved there itself.
//
// THE RULE IN ONE PLACE. A sheet's buttons and the menu page's both paint
// themselves lit by it, and carried a copy each until PR #36's review (finding
// #12). The menu bar's buttons do not: they are QToolButtons, reached only by Tab,
// with a rule of their own (MenuBarButton::isLit()).
class LitButton : public QPushButton
{
    Q_OBJECT

public:
    explicit LitButton(const QString& text, QWidget* parent = nullptr);

    bool isLit() const;

protected:
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    // Whether the keyboard came here by Tab, and has not left since.
    bool tabbedTo = false;
};

#endif
