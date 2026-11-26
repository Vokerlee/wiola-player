/**
 * @file
 * @brief Component registration and layout builder regression tests.
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
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <qt/tests/support/atomic_output.hpp>
#include <qt/widgets/layout/layout_builder.hpp>
#include <qt/widgets/layout/layout_session.hpp>
#include <wiola/qt/paths.hpp>

namespace {

using namespace wiola::gui;

LayoutDocument document(const char* id)
{
    return {
        .root = {.id = QString::fromLatin1(id), .kind = LayoutComponentKind::status},
        .node_count = 1
    };
}

std::filesystem::path path(const QTemporaryDir& directory)
{
    return wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath("layout.json"));
}

QByteArray bytes(const QTemporaryDir& directory)
{
    QFile file{directory.filePath("layout.json")};
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void overwrite(const QTemporaryDir& directory, QByteArrayView bytes)
{
    QFile file{directory.filePath("layout.json")};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(bytes.data(), bytes.size()), bytes.size());
}

using FaultOutput = wiola::testing::FaultAtomicOutput;

TEST(LayoutSession, AcceptedCustomDocumentAndIdentitySurviveRestartExactly)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QWidget owner;
    const auto custom = document("customRoot");
    {
        LayoutSession session{path(directory), document("fallback"), LayoutChoice::classic};
        LayoutBuilder builder;
        auto accepted = session.accept(custom, LayoutChoice::custom,
            builder.prepare(custom.root, {}, &owner), &owner);
        ASSERT_EQ(accepted.error, LayoutSessionError::none);
        ASSERT_NE(accepted.root, nullptr);
        EXPECT_EQ(write_layout_json(session.document()), write_layout_json(custom));
    }
    LayoutSession restored{path(directory), document("fallback"), LayoutChoice::classic};
    EXPECT_EQ(restored.choice(), LayoutChoice::custom);
    EXPECT_EQ(write_layout_json(restored.document()), write_layout_json(custom));
    EXPECT_FALSE(restored.recovered_backup());
}

TEST(LayoutSession, FailedDurableSaveRetainsAcceptedValuesAndCurrentWidgets)
{
    QTemporaryDir directory;
    QWidget owner;
    bool fail{};
    LayoutSession session{path(directory), document("fallback"), LayoutChoice::classic,
        [&](const auto& path) { return std::make_unique<FaultOutput>(path, fail); }};
    LayoutBuilder builder;
    auto original = document("first");
    auto first = session.accept(original, LayoutChoice::custom,
        builder.prepare(original.root, {}, &owner), &owner);
    ASSERT_NE(first.root, nullptr);
    QPointer<QWidget> witness = first.root;
    const auto before = bytes(directory);
    fail = true;
    auto next = document("second");
    auto failed =
        session.accept(next, LayoutChoice::custom, builder.prepare(next.root, {}, &owner), &owner);
    EXPECT_EQ(failed.error, LayoutSessionError::write_failed);
    EXPECT_EQ(failed.root, nullptr);
    EXPECT_NE(witness, nullptr);
    EXPECT_EQ(session.document().root.id, "first");
    EXPECT_EQ(bytes(directory), before);
}

TEST(LayoutSession, CorruptPrimaryRecoversBackupAndFutureSchemasRemainUnchanged)
{
    QTemporaryDir directory;
    QWidget owner;
    LayoutBuilder builder;
    {
        LayoutSession session{path(directory), document("fallback"), LayoutChoice::classic};
        for (const auto id : {"first", "second"}) {
            auto next = document(id);
            ASSERT_NE(session
                          .accept(next, LayoutChoice::custom,
                              builder.prepare(next.root, {}, &owner), &owner)
                          .root,
                nullptr);
        }
    }
    overwrite(directory, "broken");
    LayoutSession recovered{path(directory), document("fallback"), LayoutChoice::classic};
    EXPECT_TRUE(recovered.recovered_backup());
    EXPECT_EQ(recovered.document().root.id, "first");
    EXPECT_EQ(recovered.restore_error(), LayoutSessionError::invalid);
    const QByteArray future = R"({"schema":99,"future":"preserved"})";
    overwrite(directory, future);
    LayoutSession newer{path(directory), document("fallback"), LayoutChoice::classic};
    auto next = document("third");
    EXPECT_EQ(
        newer.accept(next, LayoutChoice::custom, builder.prepare(next.root, {}, &owner), &owner)
            .error,
        LayoutSessionError::unsupported_version
    );
    EXPECT_EQ(bytes(directory), future);
}

TEST(LayoutSession, RejectsMismatchedPreparedTreesBeforeWritingAndRecoversALostViewAtCommit)
{
    QTemporaryDir directory;
    QWidget owner;
    LayoutBuilder builder;
    LayoutSession session{path(directory), document("fallback"), LayoutChoice::classic};
    auto first = document("first"), second = document("second");
    EXPECT_EQ(session
                  .accept(first, LayoutChoice::custom, builder.prepare(second.root, {}, &owner),
                      &owner)
                  .error,
        LayoutSessionError::invalid);
    EXPECT_TRUE(bytes(directory).isEmpty());
    bool fail{}, armed{};
    QPointer<QWidget> live = new QWidget{&owner};
    LayoutSession reentrant{path(directory), document("fallback"), LayoutChoice::classic,
        [&](const auto& target) {
            return std::make_unique<FaultOutput>(target, fail, [&] {
                if (armed && live)
                    delete live.data();
            });
        }};
    ASSERT_TRUE(builder.views().add(component_view("live"), live, LayoutComponentKind::status));
    first.root.view = component_view("live");
    auto prepared = builder.prepare(first.root, {}, &owner);
    ASSERT_NE(prepared, nullptr);
    armed = true;
    EXPECT_EQ(reentrant.accept(first, LayoutChoice::custom, std::move(prepared), &owner).error,
        LayoutSessionError::unavailable_views);
    EXPECT_EQ(reentrant.document().root.id, "fallback");
    LayoutSession restored{path(directory), document("unused"), LayoutChoice::classic};
    EXPECT_EQ(restored.document().root.id, "fallback");
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
