#pragma once

#include <QtWidgets>

extern const Qt::WindowFlags kDialogFlags;

bool showMessageDialog(QWidget* parent, const QString& title, const QString& text, bool isQuestion = false);
QString showItemDialog(QWidget* parent, const QString& title, const QString& label,
                       const QStringList& items, bool* ok);
int showIntDialog(QWidget* parent, const QString& title, const QString& label,
                  int value, int minVal, int maxVal, int step, bool* ok);
QString formatRestartInterval(int secs);
