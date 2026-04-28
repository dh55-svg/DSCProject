#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QDockWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QTimer>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <memory>

#include "application/DataController.h"
#include "application/AlarmController.h"
#include "application/AuthController.h"
#include "infrastructure/logging/ILogger.h"
#include "infrastructure/monitoring/PerformanceMonitor.h"

class PidScene;
class TrendWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(DataController& dataCtrl, AlarmController& alarmCtrl,
               AuthController& authCtrl, ILogger* logger = nullptr);
    ~MainWindow();

private slots:
    void onRefreshTimer();
    void onDataUpdated();
    void onAlarmCountChanged(int active, int unack);
    void onConnectAll();
    void onDisconnectAll();
    void onAckAll();
    void onToggleSound();
    void onLogin();
    void onLogout();
    void onExportData();
    void onShowTagConfig();
    void onShowPerformanceMonitor();
    void onOpenProject();
    void onSaveProject();
    void onAbout();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupDockWidgets();
    void setupStatusBar();
    void setupConnections();
    void refreshDataTable();
    void refreshAlarmTable();
    void refreshDeviceTree();

    DataController& m_dataCtrl;
    AlarmController& m_alarmCtrl;
    AuthController& m_authCtrl;
    ILogger* m_logger;
    PerformanceMonitor m_perfMon;

    QTabWidget* m_tabWidget = nullptr;
    PidScene* m_pidScene = nullptr;
    QTableWidget* m_dataTable = nullptr;
    QTextEdit* m_logView = nullptr;
    TrendWidget* m_trendWidget = nullptr;

    QDockWidget* m_deviceDock = nullptr;
    QTreeWidget* m_deviceTree = nullptr;
    QDockWidget* m_alarmDock = nullptr;
    QTableWidget* m_alarmTable = nullptr;

    QTimer* m_refreshTimer = nullptr;
    QLabel* m_statusDevices = nullptr;
    QLabel* m_statusTags = nullptr;
    QLabel* m_statusAlarms = nullptr;
    QLabel* m_statusUser = nullptr;
    QLabel* m_statusTime = nullptr;

    QString m_lastProjectPath;
};
