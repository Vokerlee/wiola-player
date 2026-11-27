/**
 * @file
 * @brief The bar a listener moves through a track with.
 * @author Roman Glaz
 * @copyright (c) 2026, <vokerlee@gmail.com>
 *
 * Wiola is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wiola is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wiola. If not, see <http://www.gnu.org/licenses/>.
 */

#include "seek_bar.hpp"

#include <qt/widgets/appearance/presentation_tokens.hpp>

#include <QAccessibleValueInterface>
#include <QAccessibleWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace wiola::gui {

namespace {

constexpr PresentationTokens tokens;

} // namespace

namespace {

class AccessibleSeek final : public QAccessibleWidget,
                             public QAccessibleValueInterface {
public:
    explicit AccessibleSeek(SeekBar* bar)
        : QAccessibleWidget{bar, QAccessible::Slider}
    {
    }

    void* interface_cast(QAccessible::InterfaceType type) override
    {
        return type == QAccessible::ValueInterface ? static_cast<QAccessibleValueInterface*>(this)
                                                   : QAccessibleWidget::interface_cast(type);
    }

    QVariant currentValue() const override { return bar()->fraction() * 100; }

    void setCurrentValue(const QVariant& value) override
    {
        bool valid{};
        const auto number = value.toDouble(&valid);

        if (valid)
            bar()->request_seek(number / 100);
    }

    QVariant maximumValue() const override { return 100; }

    QVariant minimumValue() const override { return 0; }

    QVariant minimumStepSize() const override { return 1; }

private:
    SeekBar* bar() const { return static_cast<SeekBar*>(widget()); }
};

} // namespace

SeekBar::SeekBar(QWidget* parent)
    : QWidget{parent}
{
    static const bool accessible = [] {
        QAccessible::installFactory([](const QString& name,
                                        QObject* object) -> QAccessibleInterface* {
            if (name == "wiola::gui::SeekBar")
                return new AccessibleSeek{static_cast<SeekBar*>(object)};
            return nullptr;
        });
        return true;
    }();
    static_cast<void>(accessible);
    setFixedHeight(tokens.seek.bar_height);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Playback position");
    setAccessibleDescription("Arrow keys seek one percent; Page keys seek ten percent; Home and "
                             "End seek to the endpoints.");
}

void SeekBar::set_fraction(double fraction)
{
    if (!std::isfinite(fraction))
        return;

    const double shown{std::clamp(fraction, 0.0, 1.0)};

    if (dragging_ || shown == fraction_)
        return;

    fraction_ = shown;

    QAccessibleValueChangeEvent changed{this, fraction_ * 100};
    QAccessible::updateAccessibility(&changed);
    update();
}

double SeekBar::fraction_at(int x) const noexcept
{
    if (width() <= 0)
        return 0.0;

    return std::clamp(static_cast<double>(x) / width(), 0.0, 1.0);
}

void SeekBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter{this};

    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF track{0, height() * 0.25, static_cast<qreal>(width()), height() * 0.5};
    const qreal radius = track.height() / 2;
    const qreal played = fraction_ * track.width();
    const auto& colors = palette();
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.color(tokens.colors.seek_track));
    painter.drawRoundedRect(track, radius, radius);
    painter.save();
    painter.setClipRect(QRectF{track.left(), track.top(), played, track.height()});
    painter.setBrush(colors.color(isEnabled() ? tokens.colors.seek_played
                                              : tokens.colors.seek_played_disabled));
    painter.drawRoundedRect(track, radius, radius);
    painter.restore();

    if (isEnabled()) {
        painter.setBrush(colors.color(tokens.colors.seek_edge));

        const qreal handle = std::max(radius, static_cast<qreal>(tokens.seek.edge_width * 2));
        const qreal center = std::clamp(played, handle, std::max(handle, width() - handle));
        painter.drawEllipse(QPointF{center, height() / 2.0}, handle, handle);
    }

    if (hasFocus()) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(colors.color(QPalette::Highlight));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void SeekBar::request_seek(double value)
{
    if (!isEnabled() || !std::isfinite(value))
        return;

    dragging_ = false;

    const auto previous = fraction_;

    set_fraction(value);

    if (fraction_ != previous)
        emit seek_requested(fraction_);
}

bool SeekBar::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && isEnabled()) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() == Qt::NoModifier) {
            switch (key->key()) {
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_PageUp:
            case Qt::Key_PageDown:
            case Qt::Key_Home:
            case Qt::Key_End:
                key->accept();
                return true;
            case Qt::Key_Escape:
                if (dragging_) {
                    key->accept();
                    return true;
                }
                break;
            default:
                break;
            }
        }
    }
    return QWidget::event(event);
}

void SeekBar::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled() || event->modifiers() != Qt::NoModifier) {
        QWidget::keyPressEvent(event);
        return;
    }

    double next = fraction_;

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
        next -= 0.01;
        break;
    case Qt::Key_Right:
    case Qt::Key_Up:
        next += 0.01;
        break;
    case Qt::Key_PageDown:
        next -= 0.1;
        break;
    case Qt::Key_PageUp:
        next += 0.1;
        break;
    case Qt::Key_Home:
        next = 0;
        break;
    case Qt::Key_End:
        next = 1;
        break;
    case Qt::Key_Escape:
        if (dragging_) {
            dragging_ = false;
            set_fraction(drag_origin_);
        }
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    request_seek(next);
    event->accept();
}

void SeekBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled() && dragging_) {
        dragging_ = false;
        set_fraction(drag_origin_);
    }
    QWidget::changeEvent(event);
}

void SeekBar::mousePressEvent(QMouseEvent* event)
{
    if (!isEnabled() || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    setFocus(Qt::MouseFocusReason);
    drag_origin_ = fraction_;
    dragging_ = true;
    fraction_ = fraction_at(event->position().toPoint().x());
    update();

    emit dragged();
}

void SeekBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    fraction_ = fraction_at(event->position().toPoint().x());
    update();

    emit dragged();
}

void SeekBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dragging_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    dragging_ = false;

    emit seek_requested(fraction_);
}

} // namespace wiola::gui
