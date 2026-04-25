#include "MYDSCProject.h"
#include "logger.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QSplashScreen>
#include <QtGui/QPixmap>
#include <QtCore/QElapsedTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 设置应用元信息
    app.setApplicationName("MY DCS");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("MYDSC");

    // 设置全局样式表 — 深色工业风格
    app.setStyleSheet(R"(
        QMainWindow {
            background-color: #1e1e1e;
        }
        QMenuBar {
            background-color: #2d2d2d;
            color: #e0e0e0;
            border-bottom: 1px solid #444;
        }
        QMenuBar::item:selected {
            background-color: #3a3a3a;
        }
        QMenu {
            background-color: #2d2d2d;
            color: #e0e0e0;
            border: 1px solid #444;
        }
        QMenu::item:selected {
            background-color: #0078d4;
        }
        QToolBar {
            background-color: #252525;
            border-bottom: 1px solid #444;
            spacing: 4px;
            padding: 2px;
        }
        QDockWidget {
            color: #e0e0e0;
            titlebar-close-icon: url(none);
        }
        QDockWidget::title {
            background-color: #2d2d2d;
            padding: 6px;
        }
        QTreeWidget, QTableWidget {
            background-color: #1e1e1e;
            color: #e0e0e0;
            gridline-color: #333;
            border: none;
        }
        QTreeWidget::item:hover, QTableWidget::item:hover {
            background-color: #2a2a2a;
        }
        QTreeWidget::item:selected, QTableWidget::item:selected {
            background-color: #0078d4;
        }
        QHeaderView::section {
            background-color: #2d2d2d;
            color: #e0e0e0;
            border: 1px solid #444;
            padding: 4px;
        }
        QTabWidget::pane {
            border: 1px solid #444;
            background-color: #1e1e1e;
        }
        QTabBar::tab {
            background-color: #2d2d2d;
            color: #e0e0e0;
            padding: 6px 16px;
            border: 1px solid #444;
            border-bottom: none;
        }
        QTabBar::tab:selected {
            background-color: #1e1e1e;
            border-bottom: 2px solid #0078d4;
        }
        QStatusBar {
            background-color: #252525;
            color: #e0e0e0;
            border-top: 1px solid #444;
        }
        QLabel {
            color: #e0e0e0;
        }
        QToolButton {
            color: #e0e0e0;
            background-color: #333;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 4px 8px;
            margin: 1px;
        }
        QToolButton:hover {
            background-color: #444;
        }
        QToolButton:pressed {
            background-color: #0078d4;
        }
        QScrollBar:vertical {
            background-color: #2d2d2d;
            width: 12px;
        }
        QScrollBar::handle:vertical {
            background-color: #555;
            border-radius: 4px;
            min-height: 20px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )");

    // 启动耗时统计
    QElapsedTimer startupTimer;
    startupTimer.start();

    // 初始化日志系统
    Logger::instance().setLogDir("./logs");
    Logger::instance().setLogLevel(Log_Level::Debug);
    LOG_INFO("System", "========== MY DCS 系统启动 ==========");
    LOG_INFO("System", QString("版本: %1").arg(app.applicationVersion()));
#ifdef Q_OS_WIN
    QString platform = "Windows";
#elif defined(Q_OS_LINUX)
    QString platform = "Linux";
#else
    QString platform = "Unknown";
#endif
    LOG_INFO("System", QString("平台: %1").arg(platform));

    // 显示启动画面
    QSplashScreen splash;
    splash.showMessage(QStringLiteral("正在初始化 DCS 系统..."),
        Qt::AlignBottom | Qt::AlignCenter, Qt::white);
    splash.show();
    app.processEvents();

    // 创建主窗口
    MYDSCProject window;
    window.show();

    // 关闭启动画面
    splash.finish(&window);

    qint64 elapsed = startupTimer.elapsed();
    LOG_INFO("System", QString("系统启动完成，耗时 %1ms").arg(elapsed));

    return app.exec();
}
