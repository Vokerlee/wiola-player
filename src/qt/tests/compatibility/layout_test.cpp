#include "fixture_files.hpp"
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <qt/widgets/layout/layout_document.hpp>
#include <qt/widgets/layout/layout_session.hpp>
#include <wiola/qt/paths.hpp>

namespace {

using namespace wiola;
using namespace wiola::testing::compatibility;

TEST(CompatibilityLayout, OnlyLegacyVersionMigratesTheKnownDocumentView)
{
    gui::LayoutDocument legacy;
    ASSERT_EQ(gui::read_layout_json(fixture("layout/legacy-v1.json"), legacy),
        gui::LayoutJsonError::none);
    EXPECT_EQ(legacy.version, 2);
    EXPECT_EQ(legacy.root.kind, gui::LayoutComponentKind::document_host);
    ASSERT_TRUE(legacy.root.view);
    EXPECT_EQ(*legacy.root.view, gui::component_view("documentTabs"));
    gui::LayoutDocument current;
    ASSERT_EQ(gui::read_layout_json(fixture("layout/current-v2.json"), current),
        gui::LayoutJsonError::none);
    EXPECT_EQ(current.root.kind, gui::LayoutComponentKind::stack);
    EXPECT_FALSE(current.root.view);
    EXPECT_EQ(current.node_count, 1U);
}

TEST(CompatibilityLayout, RejectedImportsDoNotReplaceTheAcceptedTree)
{
    gui::LayoutDocument document;
    ASSERT_EQ(gui::read_layout_json(fixture("layout/current-v2.json"), document),
        gui::LayoutJsonError::none);
    const auto accepted = gui::write_layout_json(document);
    for (const auto& [path, error] : {
             std::pair{"layout/invalid-v2.json", gui::LayoutJsonError::invalid_component  },
             std::pair{"layout/future.json",     gui::LayoutJsonError::unsupported_version},
             std::pair{"malformed.json",         gui::LayoutJsonError::malformed          }
    }) {
        EXPECT_EQ(gui::read_layout_json(fixture(path), document), error);
        EXPECT_EQ(gui::write_layout_json(document), accepted);
    }
}

TEST(CompatibilityLayout, RestoresAuthoredEnvelopeAndPreservesFutureSessionBytes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("layout.json");
    gui::LayoutDocument fallback;
    ASSERT_EQ(gui::read_layout_json(fixture("layout/current-v2.json"), fallback),
        gui::LayoutJsonError::none);
    overwrite(path, fixture("layout/session-v1.json"));
    {
        gui::LayoutSession session{platform::qt::QtPathAdapter::from_qt(path), fallback,
            gui::LayoutChoice::classic};
        EXPECT_EQ(session.restore_error(), gui::LayoutSessionError::none);
        EXPECT_EQ(session.choice(), gui::LayoutChoice::custom);
    }
    const auto future = fixture("layout/future-session.json");
    overwrite(path, future);
    {
        gui::LayoutSession session{platform::qt::QtPathAdapter::from_qt(path), fallback,
            gui::LayoutChoice::classic};
        EXPECT_EQ(session.restore_error(), gui::LayoutSessionError::unsupported_version);
        EXPECT_EQ(session.accept(fallback, gui::LayoutChoice::custom, nullptr, nullptr).error,
            gui::LayoutSessionError::unsupported_version);
    }
    EXPECT_EQ(contents(path), future);
}

} // namespace
