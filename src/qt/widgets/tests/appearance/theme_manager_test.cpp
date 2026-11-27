/**
 * @file
 * @brief Native semantic theme regression tests.
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

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <memory>
#include <qt/tests/support/atomic_output.hpp>
#include <qt/widgets/appearance/artwork_presentation.hpp>
#include <qt/widgets/appearance/theme_manager.hpp>

namespace {

using namespace wiola::gui;

QByteArray bytes(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

class PaletteClosure final : public QObject {
public:
    explicit PaletteClosure(std::unique_ptr<ThemeManager>& manager)
        : manager_{manager}
    {
    }

private:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::ApplicationPaletteChange)
            manager_.reset();
        return false;
    }

    std::unique_ptr<ThemeManager>& manager_;
};

TEST(ThemeManager, ChangedObserverMayReleaseTheManagerAfterAcceptance)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    QObject receiver;
    auto manager = std::make_unique<ThemeManager>(settings);
    QObject::connect(manager.get(), &ThemeManager::theme_changed, &receiver,
        [&](auto) { manager.reset(); });
    EXPECT_EQ(manager->select(ThemeChoice::dark), ThemeError::none);
    EXPECT_FALSE(manager);
    ThemeState stored;
    ASSERT_EQ(read_theme_json(bytes(directory.filePath("theme.json")), stored), ThemeError::none);
    EXPECT_EQ(stored.selected, ThemeChoice::dark);
}

TEST(ThemeManager, PaletteDeliveryMayReleaseTheManagerBeforeItsOwnNotification)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    auto manager = std::make_unique<ThemeManager>(settings);
    PaletteClosure observer{manager};
    qApp->installEventFilter(&observer);
    EXPECT_EQ(manager->select(ThemeChoice::dark), ThemeError::none);
    EXPECT_FALSE(manager);
    ThemeState stored;
    ASSERT_EQ(read_theme_json(bytes(directory.filePath("theme.json")), stored), ThemeError::none);
    EXPECT_EQ(stored.selected, ThemeChoice::dark);
}

TEST(ThemeManager, PersistsAcceptedColorsKeepsCustomAcrossPresetsAndPublishesOneGeneration)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    ThemeColors custom = theme_colors(Theme::light);
    custom.window = QColor{"#f0f0f0"};
    {
        ThemeManager manager{settings};
        int changes{};
        QObject::connect(&manager, &ThemeManager::theme_changed, &manager, [&](auto) {
            ++changes;
            EXPECT_EQ(manager.select(ThemeChoice::light), ThemeError::busy);
        });
        const auto initial = manager.generation();
        ASSERT_EQ(manager.set_custom(custom), ThemeError::none);
        EXPECT_EQ(manager.colors(), custom);
        EXPECT_EQ(manager.generation(), initial + 1);
        EXPECT_EQ(changes, 1);
        EXPECT_EQ(QApplication::palette().color(QPalette::Window), custom.window);
        EXPECT_EQ(manager.set_custom(custom), ThemeError::none);
        EXPECT_EQ(changes, 1);
        ASSERT_EQ(manager.select(ThemeChoice::dark), ThemeError::none);
        EXPECT_EQ(manager.colors(), theme_colors(Theme::dark));
        ASSERT_EQ(manager.select(ThemeChoice::custom), ThemeError::none);
        EXPECT_EQ(manager.colors(), custom);
        EXPECT_EQ(changes, 3);
    }
    ThemeManager restored{settings};
    EXPECT_EQ(restored.state().selected, ThemeChoice::custom);
    EXPECT_EQ(restored.colors(), custom);
}

TEST(ThemeManager, InvalidContrastAlphaAndFailedWritesRetainAcceptedPaletteAndGeneration)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    bool fail{};
    ThemeManager manager{settings, [&](const auto& path) {
                             return std::make_unique<wiola::testing::FaultAtomicOutput>(path, fail);
                         }};
    auto custom = theme_colors(Theme::light);
    custom.window = QColor{"#f0f0f0"};
    ASSERT_EQ(manager.set_custom(custom), ThemeError::none);
    const auto generation = manager.generation();
    const auto before = manager.export_json();
    auto invalid = custom;
    invalid.text = invalid.surface;
    EXPECT_EQ(manager.set_custom(invalid), ThemeError::invalid);
    invalid = custom;
    invalid.window.setAlpha(50);
    EXPECT_EQ(manager.set_custom(invalid), ThemeError::invalid);
    fail = true;
    EXPECT_EQ(manager.select(ThemeChoice::dark), ThemeError::io);
    EXPECT_EQ(manager.export_json(), before);
    EXPECT_EQ(manager.generation(), generation);
    EXPECT_EQ(QApplication::palette().color(QPalette::Window), custom.window);
}

TEST(ThemeManager, BoundedAllowlistedImportsAndFutureFilesPreserveTheAcceptedDefinition)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    ThemeManager manager{settings};
    ASSERT_EQ(manager.select(ThemeChoice::dark), ThemeError::none);
    auto exported = manager.export_json();
    ThemeState decoded;
    ASSERT_EQ(read_theme_json(exported, decoded), ThemeError::none);
    EXPECT_EQ(decoded, manager.state());
    auto document = QJsonDocument::fromJson(exported).object();
    document.insert("execute", "anything");
    EXPECT_EQ(manager.import_json(QJsonDocument{document}.toJson()), ThemeError::invalid);
    EXPECT_EQ(manager.import_json(QByteArray(16 * 1024 + 1, ' ')), ThemeError::invalid);
    EXPECT_EQ(manager.import_json(R"({"version":99})"), ThemeError::unsupported_version);
    EXPECT_EQ(manager.export_json(), exported);
    const QByteArray future = R"({"version":99,"definition":"preserved"})";
    {
        QFile file{directory.filePath("theme.json")};
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(future), future.size());
    }
    ThemeManager newer{settings};
    EXPECT_EQ(newer.restore_error(), ThemeError::unsupported_version);
    EXPECT_EQ(newer.select(ThemeChoice::custom), ThemeError::unsupported_version);
    EXPECT_EQ(bytes(directory.filePath("theme.json")), future);
}

TEST(ThemeManager, PresetsAndCustomMetricsRoundTripWithoutLosingPersonalAppearance)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    const ThemeStyle personal{5, 9, 115};
    {
        ThemeManager manager{settings};
        ASSERT_EQ(manager.set_custom(theme_colors(Theme::dark), personal), ThemeError::none);
        for (const auto& preset : theme_presets()) {
            ASSERT_EQ(manager.select(preset.choice), ThemeError::none);
            EXPECT_EQ(manager.style(), preset.style);
            EXPECT_EQ(manager.colors(), theme_colors(preset.palette));
            ThemeState decoded;
            ASSERT_EQ(read_theme_json(manager.export_json(), decoded), ThemeError::none);
            EXPECT_EQ(decoded, manager.state());
            EXPECT_EQ(decoded.custom_style, personal);
        }
        ASSERT_EQ(manager.select(ThemeChoice::custom), ThemeError::none);
        EXPECT_EQ(manager.style(), personal);
    }
    ThemeManager restored{settings};
    EXPECT_EQ(restored.style(), personal);
    EXPECT_EQ(restored.colors(), theme_colors(Theme::dark));
    const auto accepted = restored.export_json();
    auto invalid = personal;
    invalid.corner_radius = -1;
    EXPECT_EQ(restored.set_custom(restored.colors(), invalid), ThemeError::invalid);
    EXPECT_EQ(restored.export_json(), accepted);
}

TEST(ThemeManager, LegacyColorsMigrateAndMalformedMetricsNeverReplaceAcceptedState)
{
    ThemeState accepted;
    auto object = QJsonDocument::fromJson(write_theme_json(accepted)).object();
    object.insert("version", 1);
    object.remove("style");
    ThemeState decoded;
    ASSERT_EQ(read_theme_json(QJsonDocument{object}.toJson(), decoded), ThemeError::none);
    EXPECT_EQ(decoded, accepted);
    object = QJsonDocument::fromJson(write_theme_json(accepted)).object();
    for (const auto& metric : theme_style_metrics()) {
        for (const QJsonValue value :
            {QJsonValue{metric.minimum - 1}, QJsonValue{metric.maximum + 1},
                QJsonValue{metric.minimum + 0.5}, QJsonValue{"invalid"}, QJsonValue{}}) {
            auto invalid = object;
            auto style = invalid.value("style").toObject();
            style.insert(metric.key, value);
            invalid.insert("style", style);
            EXPECT_EQ(read_theme_json(QJsonDocument{invalid}.toJson(), decoded),
                ThemeError::invalid);
            EXPECT_EQ(decoded, accepted);
        }
        auto invalid = object;
        auto style = invalid.value("style").toObject();
        style.remove(metric.key);
        invalid.insert("style", style);
        EXPECT_EQ(read_theme_json(QJsonDocument{invalid}.toJson(), decoded), ThemeError::invalid);
        EXPECT_EQ(decoded, accepted);
    }
}

TEST(ThemeManager, SharedGuiOwnersReuseOneStateAndPaletteChangesInvalidateArtworkPlaceholders)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    std::weak_ptr<ThemeManager> observer;
    {
        auto first = ThemeManager::acquire(settings), second = ThemeManager::acquire(settings);
        ASSERT_EQ(first, second);
        observer = first;
        QWidget widget;
        widget.ensurePolished();
        ArtworkPresentationService artwork;
        const wiola::library::MediaId id{"missing"};
        const auto before = artwork.cover(widget, id, {});
        ASSERT_EQ(first->select(ThemeChoice::dark), ThemeError::none);
        QCoreApplication::processEvents();
        const auto after = artwork.cover(widget, id, {});
        EXPECT_NE(before.cacheKey(), after.cacheKey());
        EXPECT_EQ(artwork.statistics().decodes, 0);
        EXPECT_EQ(second->state().selected, ThemeChoice::dark);
    }
    EXPECT_TRUE(observer.expired());
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
