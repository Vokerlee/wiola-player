#include <qt/application/session/session_state.hpp>
#include <qt/storage/library_json.hpp>
#include <qt/widgets/layout/classic_layout.hpp>
#include <qt/widgets/layout/layout_document.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

namespace {

TEST(ReaderContracts, TruncatedAndMutatedInputsPreserveThePreviouslyAcceptedState)
{
    using namespace wiola;

    const auto layout = gui::write_layout_json(gui::classic_layout_document());
    app::SessionState session;
    session.layout_identity = "retained";

    const auto preferences = app::write_session_state(session);
    library::LibrarySnapshot library;
    library::TrackRecord record;
    record.id = {"retained"};
    library.tracks.push_back(record);

    QByteArray records;

    ASSERT_EQ(persistence::qt::write_library_json(library, records),
        persistence::qt::LibraryJsonError::none);
    ASSERT_FALSE(layout.isEmpty());
    ASSERT_FALSE(preferences.isEmpty());
    ASSERT_FALSE(records.isEmpty());

    for (const auto& bytes : {layout, preferences, records}) {
        // Every prefix for these small fixtures, followed by one-byte syntax corruptions.
        for (qsizetype length = 0; length < bytes.trimmed().size(); ++length) {
            const auto candidate = bytes.first(length);
            auto old_layout = gui::classic_layout_document();
            auto old_session = session;
            auto old_library = library;

            EXPECT_NE(gui::read_layout_json(candidate, old_layout), gui::LayoutJsonError::none)
                << length;
            EXPECT_NE(app::read_session_state(candidate, old_session), app::SessionStateError::none)
                << length;
            EXPECT_NE(
                persistence::qt::read_library_json(candidate, old_library),
                persistence::qt::LibraryJsonError::none
            ) << length;
            EXPECT_EQ(gui::write_layout_json(old_layout), layout);
            EXPECT_EQ(old_session, session);
            EXPECT_EQ(old_library.tracks, library.tracks);
        }

        auto object = QJsonDocument::fromJson(bytes).object();
        object["version"] = 1000;

        const auto future = QJsonDocument{object}.toJson(QJsonDocument::Compact);
        auto old_layout = gui::classic_layout_document();
        auto old_session = session;
        auto old_library = library;

        EXPECT_EQ(gui::read_layout_json(future, old_layout),
            gui::LayoutJsonError::unsupported_version);
        EXPECT_EQ(app::read_session_state(future, old_session),
            app::SessionStateError::unsupported_version);
        EXPECT_EQ(persistence::qt::read_library_json(future, old_library),
            persistence::qt::LibraryJsonError::unsupported_version);
        EXPECT_EQ(gui::write_layout_json(old_layout), layout);
        EXPECT_EQ(old_session, session);
        EXPECT_EQ(old_library.tracks, library.tracks);
    }
}

} // namespace
