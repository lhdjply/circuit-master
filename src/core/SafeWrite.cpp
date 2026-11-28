#include "core/SafeWrite.h"

#include <QSaveFile>

namespace csm {
namespace SafeWrite {

bool bytes(const QString& path, const QByteArray& data, QString* error)
{
    if (error)
        error->clear();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    // write() 的返回值要看：磁盘满时会少写，commit 之前就该发现
    if (file.write(data) != qint64(data.size())) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool text(const QString& path, const QString& content, QString* error)
{
    return bytes(path, content.toUtf8(), error);
}

} // namespace SafeWrite
} // namespace csm
