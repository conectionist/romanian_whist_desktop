#include "LanguageDialog.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "table/TableSpace.h"
#include "table/TableTopItem.h"
#include "ui/MainMenuPage.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QTimer>

#include <algorithm>
#include <cmath>

using language::Language;

namespace
{

// The dialog's size, and where its pieces sit in it, top to bottom: the logo, the
// English line, the Romanian one under it, and the buttons side by side.
constexpr QSize dialogSize(640, 470);
constexpr qreal logoScale = 1.4;
constexpr qreal logoTop = 36.0;
constexpr qreal titleTop = 250.0;
constexpr int titlePixels = 26;
constexpr qreal subtitleTop = 290.0;
constexpr int subtitlePixels = 20;
constexpr int buttonsTop = 344;
constexpr int buttonGap = 14;
constexpr int buttonMinimumWidth = 190;

// "Whist" is the game's name in English and in Romanian, so the logo says only that.
const QStringList logoWords{ QStringLiteral("Whist") };

}

// ---- language:: ----

std::optional<Language> language::fromCode(const QString& code)
{
    const QString trimmed = code.trimmed();

    if(trimmed.isEmpty())
        return std::nullopt;

    // QLocale reads "ro", "ro_RO" and "ro-RO" alike, and gives a code it cannot read
    // the C locale's "any language".
    switch(QLocale(trimmed).language())
    {
    case QLocale::English:
        return Language::English;

    case QLocale::Romanian:
        return Language::Romanian;

    default:
        return std::nullopt;
    }
}

Language language::fromSystem(const QStringList& uiLanguages)
{
    for(const QString& code : uiLanguages)
    {
        if(const std::optional<Language> spoken = fromCode(code))
            return *spoken;
    }

    return Language::English;
}

QString language::codeOf(Language language)
{
    return language == Language::Romanian ? QStringLiteral("ro") : QStringLiteral("en");
}

QString language::nativeName(Language language)
{
    return language == Language::Romanian ? QStringLiteral("Română") : QStringLiteral("English");
}

QString language::translationResource(Language language)
{
    if(language == Language::Romanian)
        return QStringLiteral(":/i18n/romanian_whist_desktop_ro_RO");

    return QString();
}

// ---- the dialog ----

LanguageDialog::LanguageDialog(Language preselected, QWidget* parent)
    : QDialog(parent), chosen(preselected)
{
    // THE GAME'S NAME, which needs no translating either.
    setWindowTitle(QStringLiteral("Whist"));
    setFixedSize(dialogSize);

    for(const Language offered : { Language::English, Language::Romanian })
    {
        MenuPageButton* button = new MenuPageButton(std::nullopt, language::nativeName(offered), this);
        button->setObjectName(language::codeOf(offered));
        button->setColours(theme.table);
        button->installEventFilter(this);

        connect(button, &QPushButton::pressed, this, [this]{ update(); });
        connect(button, &QPushButton::released, this, [this]{ update(); });
        connect(button, &QPushButton::clicked, this, [this, offered]
        {
            chosen = offered;
            accept();
        });

        buttons.push_back(button);
    }

    // Side by side, both as wide as the wider, centred.
    int across = buttonMinimumWidth;

    for(MenuPageButton* button : buttons)
        across = std::max(across, button->sizeHint().width());

    const int down = buttons.front()->sizeHint().height();
    int left = (dialogSize.width() - (2 * across + buttonGap)) / 2;

    for(MenuPageButton* button : buttons)
    {
        button->setGeometry(left, buttonsTop, across, down);
        left += across + buttonGap;
    }

    // THE PRESELECTED LANGUAGE HAS THE KEYBOARD, and looks it once the dialog is the
    // active window (event()).
    MenuPageButton* first = buttons[preselected == Language::Romanian ? 1 : 0];
    first->setDefault(true);
    first->setFocus(Qt::OtherFocusReason);
}

bool LanguageDialog::event(QEvent* event)
{
    if(event->type() == QEvent::WindowActivate)
    {
        // After Qt has given the focus widget its focus back for the window, which it
        // does after this event: on the next turn of the loop.
        QTimer::singleShot(0, this, [this]
        {
            QWidget* focused = focusWidget();

            if(!focused || !isActiveWindow()
               || std::find(buttons.begin(), buttons.end(), focused) == buttons.end())
                return;

            // Qt sends no focus event to the widget that already has the keyboard,
            // so it lets go and takes it again (Overlay::focusNextInside() does the same).
            focused->clearFocus();
            focused->setFocus(Qt::TabFocusReason);
            update();
        });
    }

    return QDialog::event(event);
}

void LanguageDialog::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal ratio = devicePixelRatioF();
    const Theme::TablePalette& palette = theme.table;

    // The main-menu page's room, and its logo, smaller.
    painter.drawPixmap(0, 0, theme.room(size(), ratio));

    const QRectF bounds = tabletop::logoBounds();
    const qreal plaqueHalfHeight = tablespace::logoBox.height() * tablespace::logoScale * logoScale / 2.0;
    const QPointF centre(width() / 2.0, logoTop + plaqueHalfHeight);

    painter.drawPixmap(devicepixels::snapped(painter, centre + bounds.topLeft() * logoScale), logo(ratio));

    // The question, in both languages: English over Romanian.
    const auto line = [&](const QString& text, int pixels, const QColor& colour, qreal top)
    {
        QFont font = fonts::heading();
        font.setPixelSize(pixels);

        const QFontMetricsF metrics(font);

        painter.setFont(font);
        painter.setPen(colour);
        painter.drawText(QPointF((width() - metrics.horizontalAdvance(text)) / 2.0,
                                 fonts::baselineIn(metrics, top, metrics.lineSpacing())),
                         text);
    };

    line(QStringLiteral("Choose your language"), titlePixels, palette.cream, titleTop);
    line(QStringLiteral("Alege limba"), subtitlePixels, palette.muted, subtitleTop);

    // The buttons' panels, lit as the buttons are, under the faces they paint.
    for(MenuPageButton* button : buttons)
    {
        const panel::State state = button->isLit() ? panel::State::Hovered : panel::State::Plain;
        const panel::Picture made = chrome.picture(QSizeF(button->size()), palette, state, ratio);

        if(!made.pixmap.isNull())
        {
            painter.drawPixmap(devicepixels::snapped(painter, QPointF(button->pos()) + made.offset),
                               made.pixmap);
        }
    }

    QDialog::paintEvent(event);
}

bool LanguageDialog::eventFilter(QObject* watched, QEvent* event)
{
    switch(event->type())
    {
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
        update();
        break;

    default:
        break;
    }

    return QDialog::eventFilter(watched, event);
}

const QPixmap& LanguageDialog::logo(qreal ratio) const
{
    if(!logoPicture.isNull() && logoPicture.devicePixelRatio() == ratio)
        return logoPicture;

    // The main-menu page's plaque (MainMenuPage::logo()), with the one word.
    const QRectF bounds = tabletop::logoBounds();

    QPixmap picture(QSize(static_cast<int>(std::ceil(bounds.width() * logoScale * ratio)),
                          static_cast<int>(std::ceil(bounds.height() * logoScale * ratio))));
    picture.setDevicePixelRatio(ratio);
    picture.fill(Qt::transparent);

    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(logoScale, logoScale);
    painter.translate(-bounds.topLeft());

    tabletop::paintLogo(painter, theme, QPointF(0.0, 0.0), logoWords);
    painter.end();

    logoPicture = picture;

    return logoPicture;
}
