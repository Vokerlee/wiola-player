#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

#include <stdexcept>

namespace wiola::testing::compatibility {

inline QByteArray contents(const QString& path)
{
    QFile file{path};

    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error{"Cannot read compatibility fixture: " + path.toStdString()};
    }

    return file.readAll();
}

inline QString fixture_path(const char* name)
{
    return QDir{QString::fromUtf8(WIOLA_TEST_DATA_DIR)}.filePath(QString::fromUtf8(name));
}

inline QByteArray fixture(const char* name)
{
    return contents(fixture_path(name));
}

inline void overwrite(const QString& path, const QByteArray& bytes)
{
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        throw std::runtime_error{"Cannot stage compatibility fixture: " + path.toStdString()};
    }
}

} // namespace wiola::testing::compatibility
