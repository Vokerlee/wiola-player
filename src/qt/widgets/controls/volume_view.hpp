/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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

#pragma once
#include <QPointer>
#include <QWidget>
class QSlider;
class QLabel;
class QPushButton;

namespace wiola::gui {

class ViewRegistry;

struct VolumePresentation {
    int position;
    int maximum;
    bool boosted;
};

/// A reusable volume editor; the application decides gain and persistence.
class VolumeView final : public QWidget {
    Q_OBJECT
public:
    explicit VolumeView(QWidget* parent = nullptr);
    void present(VolumePresentation state);
    void register_views(ViewRegistry& registry);
signals:
    void volume_requested(int position);
    void boost_requested(bool boosted);

private:
    QPointer<QSlider> slider_;
    QPointer<QLabel> value_;
    QPointer<QPushButton> boost_;
};

} // namespace wiola::gui
