#include "SuperGuardian.h"
#include "ProcessUtils.h"
#include "LogDatabase.h"
#include <QtWidgets>
#include <QDir>

using namespace Qt::Literals::StringLiterals;

namespace {

QString normalizeWindowsPath(const QString& path) {
    QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return trimmed;
    return QDir::toNativeSeparators(QDir::cleanPath(trimmed));
}

}

void SuperGuardian::parseAndAddFromInput() {
    QString text = lineEdit->text().trimmed();
    if (text.isEmpty()) return;
    QString progPath, progArgs;
    if (text.startsWith('"')) {
        int cq = text.indexOf('"', 1);
        if (cq > 0) { progPath = text.mid(1, cq - 1); progArgs = text.mid(cq + 1).trimmed(); }
        else progPath = text.mid(1);
    } else if (QFileInfo::exists(text)) {
        progPath = text;
    } else {
        bool found = false;
        int searchFrom = 0;
        while (searchFrom < text.length()) {
            int sp = text.indexOf(' ', searchFrom);
            if (sp < 0) break;
            QString cand = text.left(sp);
            if (QFileInfo::exists(cand) || !QStandardPaths::findExecutable(cand).isEmpty()) {
                progPath = cand; progArgs = text.mid(sp + 1).trimmed(); found = true; break;
            }
            searchFrom = sp + 1;
        }
        if (!found) progPath = text;
    }
    addProgram(progPath.trimmed(), progArgs.trimmed());
    lineEdit->clear();
}

void SuperGuardian::addProgram(const QString& path, const QString& extraArgs) {
    QString resolvedPath = normalizeWindowsPath(path);
    if (resolvedPath.isEmpty())
        return;

    QFileInfo fi(resolvedPath);
    bool isSystemTool = false;
    if (!fi.exists()) {
        QString found = QStandardPaths::findExecutable(path.trimmed());
        if (found.isEmpty()) return;
        resolvedPath = normalizeWindowsPath(found);
        isSystemTool = true;
    }
    if (!isSystemTool) {
        QString nameOnly = QFileInfo(resolvedPath).fileName();
        QString found = QStandardPaths::findExecutable(nameOnly);
        if (!found.isEmpty() && QFileInfo(found).canonicalFilePath() == QFileInfo(resolvedPath).canonicalFilePath())
            isSystemTool = true;
    }
    if (!isSystemTool && !duplicateWhitelist.contains(resolvedPath, Qt::CaseInsensitive)) {
        for (const GuardItem& it : items) if (it.path == resolvedPath) return;
    }

    GuardItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.path = resolvedPath;
    QString shortcutArgs;
    item.targetPath = normalizeWindowsPath(resolveShortcut(resolvedPath, &shortcutArgs));
    if (!extraArgs.isEmpty())
        item.launchArgs = shortcutArgs.isEmpty() ? extraArgs : (shortcutArgs + " " + extraArgs);
    else
        item.launchArgs = shortcutArgs;
    item.processName = QFileInfo(item.targetPath).fileName();
    item.guarding = false;
    int maxOrder = 0;
    for (const auto& it : items) maxOrder = qMax(maxOrder, it.insertionOrder);
    item.insertionOrder = maxOrder + 1;
    items.append(item);
    logOperation(u"\u6dfb\u52a0\u7a0b\u5e8f"_s, programId(item.processName, item.launchArgs));

    rebuildTableFromItems();
    saveSettings();
}

void SuperGuardian::trySendNotification(GuardItem& item, const QString& event, const QString& detail) {
    if (!emailEnabledAct || !emailEnabledAct->isChecked()) return;
    if (!item.emailNotify.enabled) return;
    if (!isSmtpConfigValid(smtpConfig)) return;

    bool shouldSend = false;
    if (event == "guard_triggered") shouldSend = item.emailNotify.onGuardTriggered;
    else if (event == "start_failed") {
        if (item.notifiedStartFailed) return;
        shouldSend = item.emailNotify.onStartFailed;
        if (shouldSend) item.notifiedStartFailed = true;
    }
    else if (event == "restart_failed") {
        if (item.notifiedRestartFailed) return;
        shouldSend = item.emailNotify.onScheduledRestartFailed;
        if (shouldSend) item.notifiedRestartFailed = true;
    }
    else if (event == "run_failed") {
        if (item.notifiedRunFailed) return;
        shouldSend = item.emailNotify.onScheduledRunFailed;
        if (shouldSend) item.notifiedRunFailed = true;
    }
    else if (event == "process_exited") shouldSend = item.emailNotify.onProcessExited;
    else if (event == "retry_exhausted") {
        if (item.notifiedRetryExhausted) return;
        shouldSend = item.emailNotify.onRetryExhausted;
        if (shouldSend) item.notifiedRetryExhausted = true;
    }
    if (!shouldSend) return;

    QString subject = u"[SuperGuardian] %1 - %2"_s.arg(item.processName, event);
    sendNotificationAsync(smtpConfig, subject, detail);
}
