#pragma once

#include "GuardItem.h"

// 显示单条规则编辑对话框，返回 true 表示用户确认
bool showScheduleRuleEditDialog(QWidget* parent, const ScheduleRule* existing, ScheduleRule& outRule);
