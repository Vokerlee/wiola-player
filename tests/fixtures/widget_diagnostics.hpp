/**
 * @file
 * @brief Optional widget screenshots captured at the moment a Qt test fails.
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
#include <QDir>
#include <QImage>
#include <QWidget>

#include <gtest/gtest.h>

namespace wiola::testing {

class WidgetScreenshots final : public ::testing::EmptyTestEventListener {
public:
    void OnTestStart(const ::testing::TestInfo& test) override
    {
        current_test = QString{"%1.%2"}.arg(test.test_suite_name(), test.name());

        for (QChar& character : current_test) {
            if (!character.isLetterOrNumber() && character != '_' && character != '-' &&
                character != '.')
                character = '_';
        }
    }

    void OnTestPartResult(const ::testing::TestPartResult& result) override
    {
        if (!result.failed())
            return;

        const QString directory{qEnvironmentVariable("WIOLA_GUI_FAILURE_ARTIFACTS")};

        if (directory.isEmpty() || current_test.isEmpty())
            return;

        QDir{}.mkpath(directory);

        int index{0};
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible())
                continue;

            const QString file{QString{"%1-%2.png"}.arg(current_test).arg(index++)};
            const QString path{QDir{directory}.filePath(file)};
            QImage image{widget->size(), QImage::Format_ARGB32};

            image.fill(Qt::transparent);
            widget->render(&image);

            if (!image.save(path))
                qWarning("Could not save widget failure screenshot to %s", qPrintable(path));
        }
    }

private:
    QString current_test;
};

inline void retain_widget_screenshots()
{
    ::testing::UnitTest::GetInstance()->listeners().Append(new WidgetScreenshots);
}

} // namespace wiola::testing
