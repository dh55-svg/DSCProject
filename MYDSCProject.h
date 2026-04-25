#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_MYDSCProject.h"

#include <QTreeWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QLabel>
#include <QTabWidget>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QTextEdit>
#include <QScrollBar>
#include <QSlider>
#include <QComboBox>

#include "logger.h"
#include "DataEngine.h"
#include "AlarmEngine.h"
#include "AuthManager.h"
#include "PidScene.h"
#include "PidView.h"
#include "PerformanceMonitor.h"
#include "TrendWidget.h"

class MYDSCProject : public QMainWindow
{
    Q_OBJECT

public:
    MYDSCProject(QWidget *parent = nullptr);
    ~MYDSCProject();

private slots:
    // 文件菜单
    void onOpenProject();
    void onSaveProject();
    void onExportData();

    // 视图菜单
    void onToggleFullScreen();
    void onToggleLeftDock();
    void onToggleRightDock();

    // 工具菜单
    void onConnectAll();
    void onDisconnectAll();
    void onAcknowledgeAllAlarms();
    void onToggleSound();
    void onShowPerformanceMonitor();
    void onShowTagConfig();
    void onGenerateReport();

    // 帮助菜单
    void onAbout();

    // 数据更新
    void onDataUpdated();
    void onAlarmTriggered(quint32 tagId, AlarmLimit state, float value, float limit);
    void onAlarmCleared(quint32 tagId);
    void onAlarmCountChanged(int count);

    // 设备状态
    void onDeviceStatusChanged(int deviceId, bool connected);
    void onCommStatusChanged(bool connected);

    // 用户登录
    void onUserLoggedIn(const QString& username, UserLevel level);
    void onUserLoggedOut();
    void onLoginAction();

    // 定时刷新
    void onRefreshTimer();

    // 报警确认
    void onAcknowledgeAlarm();

    // 上下文菜单
    void onAlarmTableContextMenu(const QPoint& pos);
    void onDeviceTreeContextMenu(const QPoint& pos);
    void onPidViewContextMenu(const QPoint& pos);

    // 演示模式
    void onToggleDemo();

    // P&ID 缩放
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomSliderChanged(int value);

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupDockWidgets();
    void setupStatusBar();
    void setupConnections();
    void initEngines();
    void loadDemoData();

    void updateAlarmBar();
    void updateStatusBar();
    void updateDeviceTree();
    void updateDataTable();
    void updatePidItems();
    void appendLog(Log_Level level, const QString& model, const QString& message);
    void rebuildDataTable();
    void rebuildDeviceTree();
    void updateDeviceValues();

    Ui::MYDSCProjectClass ui;

    // ==== 核心引擎 ====
    DataEngine* m_dataEngine = nullptr;

    // ==== 中央区域 ====
    QTabWidget* m_centralTabs = nullptr;
    PidScene* m_pidScene = nullptr;
    QGraphicsView* m_pidView = nullptr;
    QTableWidget* m_dataTable = nullptr;
    QTextEdit* m_logView = nullptr;
    TrendWidget* m_trendWidget = nullptr;
    QWidget* m_pidContainer = nullptr;
    QSlider* m_zoomSlider = nullptr;
    QLabel* m_zoomLabel = nullptr;

    // ==== 左侧面板 ====
    QDockWidget* m_leftDock = nullptr;
    QTreeWidget* m_deviceTree = nullptr;

    // ==== 右侧面板 ====
    QDockWidget* m_rightDock = nullptr;
    QTableWidget* m_alarmTable = nullptr;

    // ==== 底部报警条 ====
    QLabel* m_alarmBar = nullptr;

    // ==== 工具栏Action ====
    QAction* m_actConnect = nullptr;
    QAction* m_actDisconnect = nullptr;
    QAction* m_actAckAll = nullptr;
    QAction* m_actSound = nullptr;
    QAction* m_actLogin = nullptr;

    // ==== 状态栏 ====
    QLabel* m_statusDevices = nullptr;
    QLabel* m_statusTags = nullptr;
    QLabel* m_statusAlarms = nullptr;
    QLabel* m_statusUser = nullptr;
    QLabel* m_statusTime = nullptr;

    // ==== 定时器 ====
    QTimer* m_refreshTimer = nullptr;
    QTimer* m_demoTimer = nullptr;

    // ==== 状态 ====
    bool m_soundEnabled = true;
    bool m_isFullScreen = false;
    bool m_demoMode = false;
    int m_totalAlarms = 0;
    QAction* m_actDemo = nullptr;
};
