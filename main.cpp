#include <QApplication>
#include "app/AppConfig.h"
#include "app/ApplicationBuilder.h"
#include "application/DataController.h"
#include "application/AlarmController.h"
#include "application/AuthController.h"
#include "presentation/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Dark industrial stylesheet
    app.setStyleSheet(R"(
        QMainWindow { background-color: #1a1a2e; }
        QMenuBar { background-color: #16213e; color: #e0e0e0; }
        QMenuBar::item:selected { background-color: #0f3460; }
        QMenu { background-color: #16213e; color: #e0e0e0; }
        QMenu::item:selected { background-color: #0f3460; }
        QToolBar { background-color: #1a1a2e; border: none; spacing: 4px; }
        QDockWidget { color: #e0e0e0; }
        QDockWidget::title { background-color: #16213e; padding: 6px; }
        QTableWidget { background-color: #1a1a2e; color: #e0e0e0; gridline-color: #2a2a4e; }
        QHeaderView::section { background-color: #16213e; color: #e0e0e0; padding: 4px; }
        QStatusBar { background-color: #16213e; color: #e0e0e0; }
        QTabWidget::pane { border: 1px solid #2a2a4e; }
        QTabBar::tab { background-color: #16213e; color: #e0e0e0; padding: 8px 16px; }
        QTabBar::tab:selected { background-color: #0f3460; }
    )");

    // 1. Load config
    auto configRepo = std::make_shared<JsonConfigRepo>();
    auto appCfg = AppConfig::fromJson("config/app.json", *configRepo);

    // 2. Build the dependency graph
    auto ctx = ApplicationBuilder()
        .withConfig(appCfg)
        .withLogger()
        .withConfigRepo()
        .withFieldbus()
        .withDatabase()
        .withDomain()
        .withPipeline()
        .build();

    // 3. Load tag configuration
    ctx->tagManager->loadFromJson("config/tags.json");

    // 4. Controllers
    DataController dataCtrl(*ctx->dataPipeline, *ctx->tagManager, *ctx->alarmEngine, *ctx->fieldbus, ctx->logger.get());
    AlarmController alarmCtrl(*ctx->alarmEngine, ctx->logger.get());
    AuthController authCtrl(*ctx->authManager, ctx->logger.get());

    // 5. Main window
    MainWindow window(dataCtrl, alarmCtrl, authCtrl, ctx->logger.get());
    window.show();

    return app.exec();
}
