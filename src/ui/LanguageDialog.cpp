#include "LanguageDialog.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "table/TableSpace.h"
#include "table/TableTopItem.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <QPolygonF>
#include <QTimer>

#include <algorithm>
#include <cmath>

using language::Language;

namespace
{

// The dialog's size, and where its pieces sit in it, top to bottom: the logo, the
// two buttons side by side, and each one's name under it.
constexpr QSize dialogSize(560, 420);
constexpr qreal logoScale = 1.4;
constexpr qreal logoTop = 32.0;
constexpr int buttonsTop = 248;
constexpr int buttonGap = 32;

// A button: the flag at 3:2, inside the panel's border and some room round it.
constexpr QSize buttonSize(160, 112);
constexpr QSizeF flagSize(120.0, 80.0);

// The name under each button, in the buttons' heading face.
constexpr qreal nameGap = 12.0;
constexpr int namePixels = 21;

// Left to right: Romanian first, the game's own language and the one lit to start.
constexpr Language offered[] = { Language::Romanian, Language::English };

// "Whist" is the game's name in English and in Romanian, so the logo says only that.
const QStringList logoWords{ QStringLiteral("Whist") };

QFont nameFont()
{
    QFont font = fonts::heading();
    font.setPixelSize(namePixels);
    return font;
}

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

void language::paintFlag(QPainter& painter, const QRectF& box, Language language)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    if(language == Language::Romanian)
    {
        // Three vertical bands of one width: blue at the hoist, yellow, red.
        const QColor blue(0x00, 0x2B, 0x7F);
        const QColor yellow(0xFC, 0xD1, 0x16);
        const QColor red(0xCE, 0x11, 0x26);

        const qreal third = box.width() / 3.0;

        painter.fillRect(QRectF(box.left(), box.top(), third, box.height()), blue);
        painter.fillRect(QRectF(box.left() + third, box.top(), third, box.height()), yellow);
        painter.fillRect(QRectF(box.left() + 2.0 * third, box.top(), box.width() - 2.0 * third, box.height()), red);
    }
    else
    {
        // THE UNION FLAG, drawn on its usual 60 x 30 grid stretched to the box: the
        // blue field; the white saltire, 6 wide; the red saltire, counterchanged -
        // a stroke 4 wide cut to one side of each white arm, the side turning with
        // the arms - then the white cross, 10 wide, and the red cross, 6.
        const QColor blue(0x01, 0x21, 0x69);
        const QColor red(0xC8, 0x10, 0x2E);
        const QColor white(Qt::white);

        painter.translate(box.topLeft());
        painter.scale(box.width() / 60.0, box.height() / 30.0);

        const QRectF field(0.0, 0.0, 60.0, 30.0);
        QPainterPath inside;
        inside.addRect(field);

        painter.fillRect(field, blue);

        QPainterPath saltire;
        saltire.moveTo(0.0, 0.0);
        saltire.lineTo(60.0, 30.0);
        saltire.moveTo(60.0, 0.0);
        saltire.lineTo(0.0, 30.0);

        // FILLS, NOT CLIPS: the raster engine draws clip paths aliased. Each stroke is
        // made a shape and cut to the field (and the red to its sides) before it is
        // filled.
        const auto stroke = [&](qreal width)
        {
            QPainterPathStroker stroker;
            stroker.setWidth(width);
            stroker.setCapStyle(Qt::FlatCap);
            return stroker.createStroke(saltire).intersected(inside);
        };

        painter.fillPath(stroke(6.0), white);

        // The four triangles round the centre that hold the red's half of each arm.
        QPainterPath sides;
        sides.addPolygon(QPolygonF({ QPointF(30.0, 15.0), QPointF(60.0, 15.0), QPointF(60.0, 30.0) }));
        sides.addPolygon(QPolygonF({ QPointF(30.0, 15.0), QPointF(30.0, 30.0), QPointF(0.0, 30.0) }));
        sides.addPolygon(QPolygonF({ QPointF(30.0, 15.0), QPointF(0.0, 15.0), QPointF(0.0, 0.0) }));
        sides.addPolygon(QPolygonF({ QPointF(30.0, 15.0), QPointF(30.0, 0.0), QPointF(60.0, 0.0) }));

        painter.fillPath(stroke(4.0).intersected(sides), red);

        painter.fillRect(QRectF(25.0, 0.0, 10.0, 30.0), white);
        painter.fillRect(QRectF(0.0, 10.0, 60.0, 10.0), white);
        painter.fillRect(QRectF(27.0, 0.0, 6.0, 30.0), red);
        painter.fillRect(QRectF(0.0, 12.0, 60.0, 6.0), red);
    }

    painter.restore();

    // A hairline round it, so the yellow and the white keep an edge on the panel.
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0, 0, 0, 110), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    painter.restore();
}

// ---- the buttons ----

FlagButton::FlagButton(Language language, QWidget* parent)
    : LitButton(language::nativeName(language), parent), shown(language)
{
    setObjectName(language::codeOf(language));
}

QSize FlagButton::sizeHint() const
{
    return buttonSize;
}

QSize FlagButton::minimumSizeHint() const
{
    return buttonSize;
}

void FlagButton::paintEvent(QPaintEvent*)
{
    // The face only: the dialog paints the panel behind it, as the menu page does.
    QPainter painter(this);

    const QRectF flag(QPointF((width() - flagSize.width()) / 2.0, (height() - flagSize.height()) / 2.0),
                      flagSize);

    language::paintFlag(painter, flag, shown);
}

// ---- the dialog ----

LanguageDialog::LanguageDialog(QWidget* parent) : QDialog(parent)
{
    // THE GAME'S NAME, which needs no translating either.
    setWindowTitle(QStringLiteral("Whist"));
    setFixedSize(dialogSize);

    int left = (dialogSize.width() - (2 * buttonSize.width() + buttonGap)) / 2;

    for(const Language language : offered)
    {
        FlagButton* button = new FlagButton(language, this);
        button->installEventFilter(this);
        button->setGeometry(QRect(QPoint(left, buttonsTop), buttonSize));
        left += buttonSize.width() + buttonGap;

        connect(button, &QPushButton::pressed, this, [this]{ update(); });
        connect(button, &QPushButton::released, this, [this]{ update(); });
        connect(button, &QPushButton::clicked, this, [this, language]
        {
            chosen = language;
            accept();
        });

        buttons.push_back(button);

        // ROMANIAN HAS THE KEYBOARD, and looks it once the dialog is the active
        // window (event()).
        if(language == chosen)
        {
            button->setDefault(true);
            button->setFocus(Qt::OtherFocusReason);
        }
    }
}

QRectF LanguageDialog::nameRect(const FlagButton* button) const
{
    const QFontMetricsF metrics(nameFont());

    return QRectF(button->x(), button->y() + button->height() + nameGap, button->width(), metrics.lineSpacing());
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

    // Each button's panel, lit as the button is, under the flag it paints; and its
    // name under it, gold-bright when lit, as a button's label is.
    const QFont font = nameFont();
    const QFontMetricsF metrics(font);

    painter.setFont(font);

    for(FlagButton* button : buttons)
    {
        const bool lit = button->isLit();
        const panel::State state = lit ? panel::State::Hovered : panel::State::Plain;
        const panel::Picture made = chrome.picture(QSizeF(button->size()), palette, state, ratio);

        if(!made.pixmap.isNull())
        {
            painter.drawPixmap(devicepixels::snapped(painter, QPointF(button->pos()) + made.offset),
                               made.pixmap);
        }

        const QRectF name = nameRect(button);
        const QString text = button->text();

        painter.setPen(lit ? palette.goldBright : palette.cream);
        painter.drawText(QPointF(name.left() + (name.width() - metrics.horizontalAdvance(text)) / 2.0,
                                 fonts::baselineIn(metrics, name.top(), name.height())),
                         text);
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
