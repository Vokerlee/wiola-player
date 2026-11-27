/**
 * @file
 * @brief Registration and state tests for shared player actions.
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

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QMenu>
#include <QPointer>
#include <QTimer>
#include <gtest/gtest.h>

#include <functional>
#include <type_traits>
#include <utility>

namespace wiola::testing {

/// Answer the next real Qt modal dialog by type, without coordinates or fixed startup delays.
template<typename Dialog>
class ModalAnswer final {
public:
    explicit ModalAnswer(std::function<void(Dialog&)> answer)
    {
        elapsed_.start();
        QObject::connect(&timer_, &QTimer::timeout, [&, answer = std::move(answer)] {
            auto* active = std::is_same_v<Dialog, QMenu> ? QApplication::activePopupWidget()
                                                         : QApplication::activeModalWidget();
            if (elapsed_.elapsed() > 5000) {
                timer_.stop();
                ADD_FAILURE() << "The expected modal dialog did not accept its answer";
                if (auto* dialog = qobject_cast<QDialog*>(active))
                    dialog->reject();
                if (auto* menu = qobject_cast<QMenu*>(active))
                    menu->close();
                return;
            }

            if (auto* dialog = qobject_cast<Dialog*>(active)) {
                QPointer<Dialog> alive{dialog};
                answered_ = true;
                answer(*dialog);
                if (!alive || !alive->isVisible())
                    timer_.stop();
            }
        });
        timer_.start(1);
    }

    ~ModalAnswer() { EXPECT_TRUE(answered_) << "The command did not open the expected dialog"; }

private:
    QElapsedTimer elapsed_;
    QTimer timer_;
    bool answered_{};
};

} // namespace wiola::testing
