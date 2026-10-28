#pragma once
#include <QString>

// Optional, firmware-specific refresh selection; never changes system files.
// Returns false and keeps the backend default if compatibility checks fail.
bool configureEpaperRefresh(const QString &mode);
