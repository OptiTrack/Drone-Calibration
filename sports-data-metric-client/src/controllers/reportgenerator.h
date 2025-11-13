#ifndef REPORTGENERATOR_H
#define REPORTGENERATOR_H

#include <QObject>
#include <QtPrintSupport>

#include "../utils/fileutils.h"

class ReportGenerator : public QObject {
    Q_OBJECT

public:
    ReportGenerator();

    void printMetricsReport();
};

#endif // REPORTGENERATOR_H
