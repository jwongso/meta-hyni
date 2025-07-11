#include <QApplication>
#include <QLoggingCategory>
#include "main_window.h"
#include "logger.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application info
    app.setApplicationName("HyniGUI");
    app.setOrganizationName("Hyni");
    app.setApplicationDisplayName("Hyni - LLM Chat Interface");

    // Enable logging
    QLoggingCategory::setFilterRules("hyni.*=true");
    logger::instance().init(true, true);
    logger::instance().set_min_level(logger::Level::INFO);

    LOG_INFO("Application starting");
    LOG_DEBUG("Built on " __DATE__ " " __TIME__);
    LOG_DEBUG("Command line: " + std::string(argv[0]) +
              (argc > 1 ? " " + std::string(argv[1]) : ""));

    // Set application style
    app.setStyle("Fusion");

    MainWindow window;
    window.show();

    return app.exec();
}
