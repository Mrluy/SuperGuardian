#pragma once

#include <QString>

QString detectSystemThemeName();
void applyAppTheme(const QString& theme);
QString currentThemeName();
QString darkStyleSheet();
QString lightStyleSheet();
