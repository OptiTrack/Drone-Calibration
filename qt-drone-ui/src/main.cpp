#include "mainwindow.h"
#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QStyleFactory>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application properties
    app.setApplicationName("Qt Drone UI");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("OptiTrack");
    app.setOrganizationDomain("optitrack.com");
    
    // Set up OptiTrack Motive-inspired professional dark theme
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette motivePalette;
    
    // Main background colors - Motive's signature dark grays
    motivePalette.setColor(QPalette::Window, QColor(45, 45, 45));          // Main window background
    motivePalette.setColor(QPalette::WindowText, QColor(220, 220, 220));   // Main text color
    motivePalette.setColor(QPalette::Base, QColor(35, 35, 35));            // Input field backgrounds
    motivePalette.setColor(QPalette::AlternateBase, QColor(55, 55, 55));   // Alternate row colors
    
    // Panel and frame colors
    motivePalette.setColor(QPalette::Button, QColor(60, 60, 60));          // Button backgrounds
    motivePalette.setColor(QPalette::ButtonText, QColor(220, 220, 220));   // Button text
    motivePalette.setColor(QPalette::Text, QColor(220, 220, 220));         // General text
    
    // OptiTrack brand accent colors
    motivePalette.setColor(QPalette::Highlight, QColor(0, 122, 204));      // OptiTrack blue highlights
    motivePalette.setColor(QPalette::HighlightedText, Qt::white);          // Selected text
    motivePalette.setColor(QPalette::Link, QColor(100, 180, 255));         // Links
    motivePalette.setColor(QPalette::BrightText, QColor(255, 100, 100));   // Error/warning text
    
    // Tooltips and disabled states
    motivePalette.setColor(QPalette::ToolTipBase, QColor(70, 70, 70));
    motivePalette.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
    motivePalette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    motivePalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    app.setPalette(motivePalette);
    
    // Apply custom stylesheet for Motive-like appearance
    QString motiveStyle = R"(
        /* Main window styling */
        QMainWindow {
            background-color: #2d2d2d;
            color: #dcdcdc;
        }
        
        /* Panel and frame styling */
        QFrame {
            border: 1px solid #555555;
            background-color: #3c3c3c;
        }
        
        /* Navigation and list styling */
        QListWidget {
            background-color: #323232;
            border: 1px solid #555555;
            selection-background-color: #007acc;
            alternate-background-color: #373737;
        }
        
        QListWidget::item {
            padding: 8px 12px;
            border-bottom: 1px solid #555555;
        }
        
        QListWidget::item:selected {
            background-color: #007acc;
            color: white;
        }
        
        QListWidget::item:hover {
            background-color: #404040;
        }
        
        /* Button styling */
        QPushButton {
            background-color: #3c3c3c;
            border: 2px solid #555555;
            color: #dcdcdc;
            padding: 6px 12px;
            border-radius: 4px;
            font-weight: bold;
        }
        
        QPushButton:hover {
            background-color: #007acc;
            border-color: #0099ff;
        }
        
        QPushButton:pressed {
            background-color: #005a99;
        }
        
        QPushButton:disabled {
            background-color: #2a2a2a;
            border-color: #444444;
            color: #787878;
        }
        
        /* Tab widget styling */
        QTabWidget::pane {
            border: 1px solid #555555;
            background-color: #3c3c3c;
        }
        
        QTabBar::tab {
            background-color: #2d2d2d;
            color: #dcdcdc;
            padding: 8px 16px;
            margin-right: 2px;
            border: 1px solid #555555;
        }
        
        QTabBar::tab:selected {
            background-color: #007acc;
            color: white;
        }
        
        QTabBar::tab:hover {
            background-color: #404040;
        }
        
        /* Splitter styling */
        QSplitter::handle {
            background-color: #555555;
        }
        
        QSplitter::handle:horizontal {
            width: 3px;
        }
        
        QSplitter::handle:vertical {
            height: 3px;
        }
        
        /* Status bar styling */
        QStatusBar {
            background-color: #2d2d2d;
            border-top: 1px solid #555555;
            color: #dcdcdc;
        }
        
        /* Menu bar styling */
        QMenuBar {
            background-color: #2d2d2d;
            color: #dcdcdc;
            border-bottom: 1px solid #555555;
        }
        
        QMenuBar::item {
            padding: 6px 12px;
        }
        
        QMenuBar::item:selected {
            background-color: #007acc;
        }
        
        /* Tool bar styling */
        QToolBar {
            background-color: #3c3c3c;
            border: 1px solid #555555;
            spacing: 3px;
        }
        
        /* Progress bar styling */
        QProgressBar {
            border: 1px solid #555555;
            background-color: #2d2d2d;
            text-align: center;
            border-radius: 2px;
        }
        
        QProgressBar::chunk {
            background-color: #007acc;
            border-radius: 2px;
        }
    )";
    
    app.setStyleSheet(motiveStyle);
    
    // Set up translations
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "QtDroneUI_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            app.installTranslator(&translator);
            break;
        }
    }
    
    // Create and show main window
    MainWindow window;
    window.show();
    
    return app.exec();
}