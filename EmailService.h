#pragma once

#include <QString>

struct SmtpConfig {
    QString server;
    int port = 587;
    bool useTls = true;
    QString username;
    QString password;
    QString fromAddress;
    QString fromName;
    QString toAddress;
};

bool isSmtpConfigValid(const SmtpConfig& config);
void sendNotificationAsync(const SmtpConfig& config, const QString& subject, const QString& body);
bool sendTestEmail(const SmtpConfig& config);
