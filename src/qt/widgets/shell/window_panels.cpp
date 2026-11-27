/**
 * @file
 * @brief Movable library and contextual track-details panels.
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

#include "window_panels.hpp"

#include <QAction>
#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>

#include <algorithm>

namespace wiola::gui {

WindowPanels::WindowPanels(QMainWindow& window, QWidget& library, QWidget& inspector,
    std::vector<QWidget*> library_views)
    : QObject(&window)
    , window_{window}
    , library_{library}
    , inspector_{inspector}
    , library_name_{library.objectName()}
    , inspector_name_{inspector.objectName()}
    , library_dock_{new QDockWidget{"Library", &window}}
    , inspector_dock_{new QDockWidget{"Track details", &window}}
{
    for (auto* view : library_views)
        library_views_.emplace_back(view, view->objectName());

    window.setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    library_dock_->setObjectName("libraryDock");
    inspector_dock_->setObjectName("inspectorDock");

    for (auto* dock : {library_dock_, inspector_dock_}) {
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
            QDockWidget::DockWidgetFloatable);
    }

    library_dock_->setWidget(&library_);
    inspector_dock_->setWidget(&inspector_);
    window.addDockWidget(Qt::LeftDockWidgetArea, library_dock_);
    window.addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    inspector_dock_->hide();
    window.resizeDocks({library_dock_, inspector_dock_}, {220, 320}, Qt::Horizontal);
}

void WindowPanels::reconcile(QWidget& central)
{
    const auto borrowed = [&central](const QWidget* widget) {
        return widget == &central || central.isAncestorOf(widget);
    };
    const bool library_embedded = borrowed(&library_) ||
        std::ranges::any_of(library_views_, [&](const auto& view) { return borrowed(view.first); });
    const bool inspector_embedded = borrowed(&inspector_);
    const auto reconcile_panel = [](QDockWidget& dock, QWidget& content, bool embedded,
                                     bool was_embedded, bool& open) {
        if (embedded) {
            if (!was_embedded)
                open = !dock.isHidden();
            dock.hide();
        } else {
            if (content.parentWidget() != &dock)
                dock.setWidget(&content);

            content.show();
            dock.setVisible(was_embedded ? open : !dock.isHidden());
        }
        dock.toggleViewAction()->setEnabled(!embedded);
    };

    if (!library_embedded)
        for (const auto& [view, name] : library_views_) {
            library_.layout()->addWidget(view);
            view->setObjectName(name);
            view->show();
        }

    if (!library_embedded)
        library_.setObjectName(library_name_);

    if (!inspector_embedded)
        inspector_.setObjectName(inspector_name_);

    reconcile_panel(*library_dock_, library_, library_embedded, library_embedded_, library_open_);
    reconcile_panel(*inspector_dock_, inspector_, inspector_embedded, inspector_embedded_,
        inspector_open_);
    library_embedded_ = library_embedded;
    inspector_embedded_ = inspector_embedded;
}

bool WindowPanels::contains(const QWidget& widget) const
{
    return library_dock_->isAncestorOf(&widget) || inspector_dock_->isAncestorOf(&widget);
}

void WindowPanels::show_inspector()
{
    if (!inspector_embedded_) {
        inspector_dock_->show();
        inspector_dock_->raise();
    }

    inspector_.show();
    inspector_.setFocus(Qt::OtherFocusReason);
}

void WindowPanels::add_menu_actions(QMenu& menu)
{
    menu.addAction(library_dock_->toggleViewAction());
    menu.addAction(inspector_dock_->toggleViewAction());

    auto* reset_action = menu.addAction("Reset panel positions");
    connect(reset_action, &QAction::triggered, this, [this] { reset(); });
}

void WindowPanels::reset()
{
    for (auto* dock : {library_dock_, inspector_dock_})
        dock->setFloating(false);

    window_.addDockWidget(Qt::LeftDockWidgetArea, library_dock_);
    window_.addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    library_dock_->setVisible(!library_embedded_);
    inspector_dock_->hide();
    window_.resizeDocks({library_dock_, inspector_dock_}, {220, 320}, Qt::Horizontal);
}

} // namespace wiola::gui
