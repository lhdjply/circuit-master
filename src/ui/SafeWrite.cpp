#include "ui/SafeWrite.h"

#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QSaveFile>

namespace csm {
namespace SafeWrite {

bool image(const QImage& image, const QString& path, QString* error)
{
    const QByteArray format =
        QFileInfo(path).suffix().toUpper().toLatin1();   // png → PNG
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    // 让编码器直接写到 QSaveFile 上，省一次内存拷贝
    QImageWriter writer(&file, format);
    if (!writer.write(image)) {
        if (error)
            *error = writer.errorString();
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

} // namespace SafeWrite
} // namespace csm
