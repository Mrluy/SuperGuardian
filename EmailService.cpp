#include "EmailService.h"
#include <QProcess>
#include <QObject>

static QString esc(const QString& s) {
    QString r = s;
    r.replace("'", "''");
    return r;
}

static QString buildPsScript(const SmtpConfig& config, const QString& subject, const QString& body) {
    QStringList p;
    p << "try {";
    p << QString("$smtp = New-Object System.Net.Mail.SmtpClient('%1', %2);").arg(esc(config.server)).arg(config.port);
    if (config.useTls)
        p << "$smtp.EnableSsl = $true;";
    if (!config.username.isEmpty())
        p << QString("$smtp.Credentials = New-Object System.Net.NetworkCredential('%1', '%2');").arg(esc(config.username), esc(config.password));
    p << QString("$msg = New-Object System.Net.Mail.MailMessage;");
    p << QString("$msg.From = New-Object System.Net.Mail.MailAddress('%1', '%2');").arg(esc(config.fromAddress), esc(config.fromName));
    p << QString("$msg.To.Add('%1');").arg(esc(config.toAddress));
    p << QString("$msg.Subject = '%1';").arg(esc(subject));
    p << QString("$msg.Body = '%1';").arg(esc(body));
    p << "$msg.SubjectEncoding = [System.Text.Encoding]::UTF8;";
    p << "$msg.BodyEncoding = [System.Text.Encoding]::UTF8;";
    p << "$smtp.Send($msg);";
    p << "} catch { exit 1 }";
    return p.join(" ");
}

bool isSmtpConfigValid(const SmtpConfig& config) {
    return !config.server.isEmpty() && config.port > 0
        && !config.fromAddress.isEmpty() && !config.toAddress.isEmpty();
}

void sendNotificationAsync(const SmtpConfig& config, const QString& subject, const QString& body) {
    if (!isSmtpConfigValid(config)) return;
    QProcess* proc = new QProcess();
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, &QProcess::deleteLater);
    proc->start("powershell", { "-NoProfile", "-NonInteractive", "-Command", buildPsScript(config, subject, body) });
}

bool sendTestEmail(const SmtpConfig& config) {
    if (!isSmtpConfigValid(config)) return false;
    QProcess proc;
    proc.start("powershell", { "-NoProfile", "-NonInteractive", "-Command",
        buildPsScript(config, QString::fromUtf8("SuperGuardian 测试邮件"), QString::fromUtf8("这是一封来自超级守护的测试邮件。")) });
    proc.waitForFinished(30000);
    return proc.exitCode() == 0;
}
