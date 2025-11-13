#include "reportgenerator.h"

ReportGenerator::ReportGenerator() {}


void ReportGenerator::printMetricsReport() {
    qDebug() << "Printing file";
    QString html = loadStyleSheet(":/html/src/assets/html/report.html");

    if (html.isEmpty()) {
        qWarning() << "ReportGenerator: Failed to load HTML content";
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPageSize(QPageSize(QPageSize::Letter));
    printer.setFullPage(true);
    printer.setPageMargins(QMarginsF(0.0, 0.0, 0.0, 0.0), QPageLayout::Inch);
    printer.setOutputFileName("SportMetricsReport.pdf");

    QTextDocument doc;
    doc.setHtml(html);

    QSizeF printableSizeMM = printer.pageLayout().paintRect().size();
    QSizeF pageSizePoints((printableSizeMM.width() * 25.4) * 72.0,
                          (printableSizeMM.height() * 25.4) * 72.0);

    doc.setPageSize(pageSizePoints);
    doc.print(&printer);
}
