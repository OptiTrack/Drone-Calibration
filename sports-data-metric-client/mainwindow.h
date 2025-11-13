#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDebug>
#include <QComboBox>
#include <QTableWidgetItem>
#include <QThread>
#include <QJsonArray>
#include <QtPrintSupport/QPrinter>

#include "metrics_data.h"
#include "skeleton_metrics.h"
#include "./src/controllers/toggles.h"
#include "./src/controllers/metricscontroller.h"
#include "./src/controllers/streamingcontroller.h"
#include "./src/connection/connection_controller.h"
#include "./src/rendering/glwidget.h"
#include "./src/controllers/metricsmanager.h"
#include "./src/widgets/graphwidget.h"
#include "./src/utils/fileutils.h"
#include "./src/controllers/reportgenerator.h"
#include "./src/controllers/configurecontroller.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    /**
     * @brief Returns the OpenGL rendering widget.
     */
    GLWidget* getOpenGLWidget();

    /**
     * @brief Returns the Streaming Controller object
     */
    StreamingController* getStreamingController();

    /**
     * @brief Returns the Configure Controller object
     */
    ConfigureController* getConfigureController();

    /**
     * @brief Returns the rigidMetricsManager object
     */
    MetricsManager* getRigidMetricsManager();

    /**
     * @brief Returns the bodyMetricsManager object
     */
    MetricsManager* getBodyMetricsManager();

    /**
     * @brief Set the Connection Controller object
     *
     * @param controller
     */
    void setConnectionController(ConnectionController* controller);

private:
    Ui::MainWindow *ui;

    ConnectionController* m_controller = nullptr;
    StreamingController *streamingController = nullptr;
    ConfigureController *configureController = nullptr;
    MetricsManager *rigidMetricsManager = nullptr;
    MetricsManager *bodyMetricsManager = nullptr;
    ReportGenerator *reportGenerator = nullptr;

    /**
     * @brief Sets up all tab toggles.
     */
    void setupToggles();

    /**
     * @brief Sets up the streaming controller.
     */
    void setupStreamingController();

    /**
     * @brief Sets up the configure controller.
     */
    void setupConfigureController();

    /**
     * @brief Sets up the metrics manager that handles all metric controllers.
     */
    void setupMetricsManager();

    /**
     * @brief Sets up report generator.
     */
    void setupReportGenerator();

    /**
     * @brief Sets up signals/slots.
     */
    void setupSignalSlots();

    /**
     * @brief Sets up the initial sport settings.
     */
    void setupSportSettings();
};
#endif // MAINWINDOW_H
