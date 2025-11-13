#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>

struct ConnectionSettings {
    QString serverIP = "127.0.0.1";
    QString clientIP = "127.0.0.1";
    QString connectionType = "Multicast";
    QString namingConvention = "FBX";
};

#endif // SETTINGS_H
