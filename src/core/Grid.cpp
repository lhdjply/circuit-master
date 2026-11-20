#include "core/Grid.h"

#include <QSettings>

#include <algorithm>

namespace csm {
namespace Grid {

namespace {
double g_size = 10.0;

double clamp(double value)
{
    return std::clamp(value, 1.0, 100.0);
}
} // namespace

double size()
{
    return g_size;
}

void setSize(double value)
{
    g_size = clamp(value);
    QSettings().setValue(QStringLiteral("grid/size"), g_size);
}

void load()
{
    const double saved = QSettings().value(QStringLiteral("grid/size"), 10.0).toDouble();
    g_size = clamp(saved > 0.0 ? saved : 10.0);
}

} // namespace Grid
} // namespace csm
