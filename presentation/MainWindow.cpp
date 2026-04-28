#include "MainWindow.h"
#include "presentation/PidScene.h"
#include "presentation/TrendWidget.h"
#include "presentation/TagConfigDialog.h"
#include "domain/tag/TagManager.h"
#include "domain/alarm/AlarmEngine.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QAction>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QSet>

MainWindow::MainWindow(DataController& dataCtrl, AlarmController& alarmCtrl,
                        AuthController& authCtrl, ILogger* logger)
    : QMainWindow(nullptr), m_dataCtrl(dataCtrl), m_alarmCtrl(alarmCtrl), m_authCtrl(authCtrl), m_logger(logger)
{
    setWindowTitle("MYDSC - Distributed Control System v2.0");
    resize(1400, 900);

    setupMenuBar();
    setupToolBar();
    setupCentralWidget();
    setupDockWidgets();
    setupStatusBar();
    setupConnections();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(500);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    m_refreshTimer->start();

    // Initial population
    refreshDataTable();
    refreshAlarmTable();
    refreshDeviceTree();

    if (m_logger) m_logger->info("Main window initialized");
}

MainWindow::~MainWindow() {}

void MainWindow::setupMenuBar() {
    QMenuBar* mb = menuBar();

    QMenu* fileMenu = mb->addMenu("&File");
    fileMenu->addAction("Open Project...", this, &MainWindow::onOpenProject);
    fileMenu->addAction("Save Project...", this, &MainWindow::onSaveProject);
    fileMenu->addSeparator();
    fileMenu->addAction("Export Data...", this, &MainWindow::onExportData);
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", qApp, &QApplication::quit, QKeySequence::Quit);

    QMenu* viewMenu = mb->addMenu("&View");
    QAction* fullScreenAction = viewMenu->addAction("Full Screen");
    fullScreenAction->setShortcut(QKeySequence("F11"));
    connect(fullScreenAction, &QAction::triggered, this, [this]() {
        if (isFullScreen()) showNormal(); else showFullScreen();
    });
    viewMenu->addAction("Toggle Navigation Panel", this, [this]() {
        if (m_deviceDock) m_deviceDock->setVisible(!m_deviceDock->isVisible());
    }, QKeySequence("Ctrl+1"));
    viewMenu->addAction("Toggle Alarm Panel", this, [this]() {
        if (m_alarmDock) m_alarmDock->setVisible(!m_alarmDock->isVisible());
    }, QKeySequence("Ctrl+2"));

    QMenu* toolsMenu = mb->addMenu("&Tools");
    toolsMenu->addAction("Connect All", this, &MainWindow::onConnectAll, QKeySequence("Ctrl+L"));
    toolsMenu->addAction("Disconnect All", this, &MainWindow::onDisconnectAll, QKeySequence("Ctrl+D"));
    toolsMenu->addAction("Acknowledge All Alarms", this, &MainWindow::onAckAll, QKeySequence("Ctrl+K"));
    toolsMenu->addSeparator();
    toolsMenu->addAction("Performance Monitor...", this, &MainWindow::onShowPerformanceMonitor);
    toolsMenu->addAction("Tag Configuration...", this, &MainWindow::onShowTagConfig);

    QMenu* helpMenu = mb->addMenu("&Help");
    helpMenu->addAction("About", this, &MainWindow::onAbout);
}

void MainWindow::setupToolBar() {
    QToolBar* tb = addToolBar("Main");
    tb->addAction("Connect", this, &MainWindow::onConnectAll);
    tb->addAction("Disconnect", this, &MainWindow::onDisconnectAll);
    tb->addSeparator();
    tb->addAction("AckAll", this, &MainWindow::onAckAll);
    tb->addAction("Sound", this, &MainWindow::onToggleSound);
    tb->addSeparator();
    tb->addAction("Login", this, &MainWindow::onLogin);
    tb->addAction("Logout", this, &MainWindow::onLogout);
}

void MainWindow::setupCentralWidget() {
    m_tabWidget = new QTabWidget(this);

    m_pidScene = new PidScene(this);
    auto* pidView = new QGraphicsView(m_pidScene, this);
    pidView->setDragMode(QGraphicsView::ScrollHandDrag);
    pidView->setRenderHint(QPainter::Antialiasing);
    m_tabWidget->addTab(pidView, "P&ID");

    m_dataTable = new QTableWidget(0, 7, this);
    m_dataTable->setHorizontalHeaderLabels({"ID", "Name", "Description", "Value", "Unit", "Quality", "Alarm"});
    m_dataTable->horizontalHeader()->setStretchLastSection(true);
    m_dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabWidget->addTab(m_dataTable, "Data Table");

    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet("background-color: #0a0a1a; color: #00ff00; font-family: monospace;");
    m_tabWidget->addTab(m_logView, "System Log");

    m_trendWidget = new TrendWidget(this);
    m_tabWidget->addTab(m_trendWidget, "Trends");

    setCentralWidget(m_tabWidget);
}

void MainWindow::setupDockWidgets() {
    m_deviceDock = new QDockWidget("Device Navigation", this);
    m_deviceTree = new QTreeWidget(m_deviceDock);
    m_deviceTree->setHeaderLabels({"Device/Name", "Status"});
    m_deviceDock->setWidget(m_deviceTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_deviceDock);

    m_alarmDock = new QDockWidget("Alarm Summary (ISA-18.2)", this);
    m_alarmTable = new QTableWidget(0, 10, m_alarmDock);
    m_alarmTable->setHorizontalHeaderLabels({"Alarm ID", "Tag", "Limit", "Priority", "Class", "State", "Value", "Threshold", "Area", "Time"});
    m_alarmTable->horizontalHeader()->setStretchLastSection(true);
    m_alarmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alarmDock->setWidget(m_alarmTable);
    addDockWidget(Qt::RightDockWidgetArea, m_alarmDock);
}

void MainWindow::setupStatusBar() {
    QStatusBar* sb = statusBar();
    m_statusDevices = new QLabel("Devices: 0/0", sb);
    m_statusTags = new QLabel("Tags: 0", sb);
    m_statusAlarms = new QLabel("Alarms: 0", sb);
    m_statusUser = new QLabel("User: --", sb);
    m_statusTime = new QLabel(QDateTime::currentDateTime().toString("hh:mm:ss"), sb);
    sb->addWidget(m_statusDevices);
    sb->addWidget(m_statusTags);
    sb->addWidget(m_statusAlarms);
    sb->addWidget(m_statusUser);
    sb->addPermanentWidget(m_statusTime);
}

void MainWindow::setupConnections() {
    connect(&m_dataCtrl, &DataController::dataUpdated, this, &MainWindow::onDataUpdated);
    connect(&m_alarmCtrl, &AlarmController::alarmCountChanged, this, &MainWindow::onAlarmCountChanged);

    if (m_logger) {
        m_logger->setLogCallback([this](const QString& msg) {
            QMetaObject::invokeMethod(this, [this, msg]() {
                m_logView->append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), msg));
            }, Qt::QueuedConnection);
        });
    }
}

void MainWindow::onRefreshTimer() {
    auto snap = m_dataCtrl.pipeline().doubleBuffer()->readAll();
    if (!snap) return;

    int tagCount = m_dataCtrl.tagManager().tagCount();
    m_statusTags->setText(QString("Tags: %1").arg(tagCount));

    int online = m_dataCtrl.pipeline().fieldbus() ? m_dataCtrl.pipeline().fieldbus()->onlineDeviceCount() : 0;
    int total = m_dataCtrl.pipeline().fieldbus() ? m_dataCtrl.pipeline().fieldbus()->totalDeviceCount() : 0;
    m_statusDevices->setText(QString("Devices: %1/%2").arg(online).arg(total));
    m_statusTime->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));

    refreshDataTable();
    refreshAlarmTable();
    refreshDeviceTree();
}

void MainWindow::onDataUpdated() {
    // Data pipeline has produced new data
}

void MainWindow::onAlarmCountChanged(int active, int unack) {
    m_statusAlarms->setText(QString("Alarms: %1 (%2 unack)").arg(active).arg(unack));
    m_statusAlarms->setStyleSheet(active > 0 ? "color: red; font-weight: bold;" : "color: green;");
}

void MainWindow::onConnectAll() {
    m_dataCtrl.connectAll();
    if (m_logger) m_logger->info("All devices connecting...");
}

void MainWindow::onDisconnectAll() {
    m_dataCtrl.disconnectAll();
    if (m_logger) m_logger->info("All devices disconnected.");
}

void MainWindow::onAckAll() {
    if (m_authCtrl.isLoggedIn())
        m_alarmCtrl.acknowledgeAll(m_authCtrl.currentUser());
    else
        m_alarmCtrl.acknowledgeAll();
}

void MainWindow::onToggleSound() {
    bool current = m_alarmCtrl.engine().soundEnabled();
    m_alarmCtrl.setSoundEnabled(!current);
    if (m_logger) m_logger->info(QString("Sound %1").arg(!current ? "enabled" : "muted"));
}

void MainWindow::onLogin() {
    bool ok;
    QString user = QInputDialog::getText(this, "Login", "Username:", QLineEdit::Normal, "", &ok);
    if (!ok || user.isEmpty()) return;
    QString pass = QInputDialog::getText(this, "Login", "Password:", QLineEdit::Password, "", &ok);
    if (!ok) return;
    if (!m_authCtrl.login(user, pass))
        QMessageBox::warning(this, "Login Failed", "Invalid username or password.");
    else
        m_statusUser->setText(QString("User: %1").arg(m_authCtrl.currentUser()));
}

void MainWindow::onLogout() {
    m_authCtrl.logout();
    m_statusUser->setText("User: --");
}

void MainWindow::onExportData() {
    QString fileName = QFileDialog::getSaveFileName(this, "Export Data",
        QString("dcs_export_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        "CSV Files (*.csv);;All Files (*)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Failed", "Cannot open file for writing.");
        return;
    }

    QTextStream out(&file);
    auto snap = m_dataCtrl.pipeline().doubleBuffer()->readAll();
    auto tags = m_dataCtrl.tagManager().getAllTags();

    out << "Tag ID,Tag Name,Current Value,Set Point,Output,Alarm State,Quality,Unit,Range Low,Range High,Timestamp\n";
    for (const auto& tag : tags) {
        auto it = snap->find(tag.tagId);
        float val = (it != snap->end()) ? it->second.currentValue : 0.0f;
        float sp = (it != snap->end()) ? it->second.setPoint : 0.0f;
        float outVal = (it != snap->end()) ? it->second.outputValue : 0.0f;
        int alarmSt = (it != snap->end()) ? static_cast<int>(it->second.alarmState) : 0;
        int qual = (it != snap->end()) ? static_cast<int>(it->second.quality) : 0;
        qint64 ts = (it != snap->end()) ? it->second.timestamp : 0;

        out << tag.tagId << "," << tag.tagName << "," << val << "," << sp << "," << outVal << ","
            << alarmSt << "," << qual << "," << tag.unit << "," << tag.engLow << "," << tag.engHigh
            << "," << ts << "\n";
    }
    file.close();

    if (m_logger) m_logger->info(QString("Exported %1 tags to %2").arg(tags.size()).arg(fileName));
    QMessageBox::information(this, "Export", QString("Exported %1 tags successfully.").arg(tags.size()));
}

void MainWindow::onShowTagConfig() {
    if (!m_authCtrl.canConfigure()) {
        QMessageBox::warning(this, "Access Denied", "Engineer level or above required.");
        return;
    }
    TagConfigDialog dlg(m_dataCtrl.tagManager(), this);
    dlg.exec();
}

void MainWindow::onShowPerformanceMonitor() {
    QString report = m_perfMon.generateReport();

    // Update with current alarm stats
    auto kpi = m_alarmCtrl.engine().kpiSnapshot();
    report += "\n--- Alarm KPIs ---\n";
    report += QString("Active Alarms: %1  Shelved: %2  Suppressed: %3  OOS: %4\n")
        .arg(m_alarmCtrl.engine().activeAlarmCount())
        .arg(m_alarmCtrl.engine().shelvedAlarms().size())
        .arg(m_alarmCtrl.engine().suppressedCount())
        .arg(m_alarmCtrl.engine().outOfServiceCount());
    report += QString("Health Score: %1 (%2)\n").arg(kpi.systemHealthScore, 0, 'f', 1).arg(kpi.healthGrade);
    report += QString("10-min Alarm Count: %1  Avg/hr: %2  Peak: %3\n")
        .arg(kpi.alarmCount10min).arg(kpi.avgPerHour, 0, 'f', 1).arg(kpi.peakCount10min);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Performance Monitor");
    msgBox.setDetailedText(report);
    msgBox.setText(QString("System Health: %1 (%2)\nActive: %3  Shelved: %4  Suppressed: %5")
        .arg(kpi.systemHealthScore, 0, 'f', 1).arg(kpi.healthGrade)
        .arg(kpi.totalActive).arg(kpi.shelvedCount).arg(kpi.suppressedCount));
    msgBox.exec();
}

void MainWindow::onOpenProject() {
    QString fileName = QFileDialog::getOpenFileName(this, "Open Tag Configuration",
        QString(), "JSON Files (*.json);;All Files (*)");
    if (fileName.isEmpty()) return;

    if (m_dataCtrl.tagManager().loadFromJson(fileName)) {
        m_lastProjectPath = fileName;
        if (m_logger) m_logger->info(QString("Opened project: %1").arg(fileName));
    } else {
        QMessageBox::warning(this, "Error", "Failed to load tag configuration.");
    }
}

void MainWindow::onSaveProject() {
    QString fileName = QFileDialog::getSaveFileName(this, "Save Tag Configuration",
        m_lastProjectPath.isEmpty() ? "tags_export.json" : m_lastProjectPath,
        "JSON Files (*.json);;All Files (*)");
    if (fileName.isEmpty()) return;

    if (m_dataCtrl.tagManager().saveToJson(fileName)) {
        m_lastProjectPath = fileName;
        if (m_logger) m_logger->info(QString("Saved project: %1").arg(fileName));
    } else {
        QMessageBox::warning(this, "Error", "Failed to save tag configuration.");
    }
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About MYDSC",
        "<h3>MYDSC v2.0</h3>"
        "<p>Modular Distributed Control System</p>"
        "<p>Industrial DCS framework with ISA-18.2 compliant alarm management, "
        "EEMUA 191 KPI monitoring, Modbus TCP/RTU fieldbus, and clean architecture design.</p>"
        "<p>Built with Qt 6 and C++17.</p>"
        "<hr>"
        "<p>Architecture: 4-layer (Presentation / Application / Domain / Infrastructure)</p>"
        "<p>License: Proprietary</p>");
}

void MainWindow::refreshDataTable() {
    auto snap = m_dataCtrl.pipeline().doubleBuffer()->readAll();
    if (!snap) return;

    auto tags = m_dataCtrl.tagManager().getAllTags();
    int rowCount = tags.size();

    static const QStringList qualityText = {"Good", "Bad", "Uncertain"};
    static const QStringList alarmText = {"Normal", "HH", "H", "L", "LL", "Dev", "ROC"};

    if (m_dataTable->rowCount() != rowCount)
        m_dataTable->setRowCount(rowCount);

    for (int row = 0; row < rowCount; ++row) {
        const auto& tag = tags[row];
        auto it = snap->find(tag.tagId);
        bool hasData = (it != snap->end());

        auto setOrCreate = [&](int col, const QString& text, const QColor& fg = QColor()) {
            auto* item = m_dataTable->item(row, col);
            if (!item) {
                item = new QTableWidgetItem();
                m_dataTable->setItem(row, col, item);
            }
            if (item->text() != text) item->setText(text);
            if (fg.isValid()) item->setForeground(fg);
        };

        setOrCreate(0, QString::number(tag.tagId));
        setOrCreate(1, tag.tagName);
        setOrCreate(2, tag.description);
        setOrCreate(3, hasData ? QString::number(it->second.currentValue, 'f', 3) : "--");
        setOrCreate(4, tag.unit);

        if (hasData) {
            int qi = qBound(0, static_cast<int>(it->second.quality), qualityText.size() - 1);
            setOrCreate(5, qualityText[qi], qi == 0 ? QColor("#00ff00") : QColor("#ff4444"));

            int ai = qBound(0, static_cast<int>(it->second.alarmState), alarmText.size() - 1);
            setOrCreate(6, alarmText[ai], ai != 0 ? QColor("#ff4444") : QColor());
        } else {
            setOrCreate(5, "Unknown", QColor("#ff4444"));
            setOrCreate(6, "--");
        }
    }
}

void MainWindow::refreshAlarmTable() {
    auto activeAlarms = m_alarmCtrl.engine().activeAlarms();
    int rowCount = activeAlarms.size();

    static const QStringList limitNames = {"Normal", "LL", "L", "H", "HH", "Dev", "ROC"};
    static const QStringList priorityNames = {"Adv", "Min", "Maj", "Crit"};
    static const QStringList classNames = {"Proc", "Safety", "Env", "Qual", "Mach", "Elec", "Instr"};
    static const QStringList stateNames = {"Norm", "Unack", "Ack", "RTNUn", "RTNAck", "Shlv", "Supp", "OOS"};

    if (m_alarmTable->rowCount() != rowCount)
        m_alarmTable->setRowCount(rowCount);

    for (int row = 0; row < rowCount; ++row) {
        const auto& alarm = activeAlarms[row];

        auto setOrCreate = [&](int col, const QString& text, const QColor& fg = QColor()) {
            auto* item = m_alarmTable->item(row, col);
            if (!item) {
                item = new QTableWidgetItem();
                m_alarmTable->setItem(row, col, item);
            }
            if (item->text() != text) item->setText(text);
            if (fg.isValid()) item->setForeground(fg);
        };

        setOrCreate(0, alarm.alarmId);
        setOrCreate(1, alarm.tagName);

        int li = static_cast<int>(alarm.limit);
        setOrCreate(2, li < limitNames.size() ? limitNames[li] : "?");

        int pi = static_cast<int>(alarm.priority);
        setOrCreate(3, pi < priorityNames.size() ? priorityNames[pi] : "?");

        int ci = static_cast<int>(alarm.classification);
        setOrCreate(4, ci < classNames.size() ? classNames[ci] : "?");

        int si = static_cast<int>(alarm.state);
        QColor stateColor;
        switch (alarm.state) {
        case AlarmState::ActiveUnacknowledged:         stateColor = QColor("#ff4444"); break;
        case AlarmState::ActiveAcknowledged:            stateColor = QColor("#ff88aa"); break;
        case AlarmState::ReturnToNormalUnacknowledged:  stateColor = QColor("#ffaa00"); break;
        case AlarmState::ReturnToNormalAcknowledged:    stateColor = QColor("#00aa00"); break;
        case AlarmState::Shelved:
        case AlarmState::SuppressedByDesign:
        case AlarmState::OutOfService:                  stateColor = QColor("#888888"); break;
        default: break;
        }
        setOrCreate(5, si < stateNames.size() ? stateNames[si] : "?", stateColor);

        setOrCreate(6, QString::number(alarm.triggerValue, 'f', 3));
        setOrCreate(7, QString::number(alarm.thresholdValue, 'f', 3));
        setOrCreate(8, alarm.area);
        setOrCreate(9, QDateTime::fromMSecsSinceEpoch(alarm.triggerTime).toString("hh:mm:ss"));
    }
}

void MainWindow::refreshDeviceTree() {
    if (!m_dataCtrl.pipeline().fieldbus()) return;

    auto tags = m_dataCtrl.tagManager().getAllTags();
    QSet<int> deviceIds;
    for (const auto& tag : tags) deviceIds.insert(tag.modbusDeviceId);

    // Keep tree in sync - rebuild if device count changed
    int existingRoots = m_deviceTree->topLevelItemCount();
    if (existingRoots != deviceIds.size() || existingRoots == 0) {
        m_deviceTree->clear();
        int online = m_dataCtrl.pipeline().fieldbus()->onlineDeviceCount();
        for (int devId : deviceIds) {
            auto* devItem = new QTreeWidgetItem(m_deviceTree);
            devItem->setText(0, QString("Device #%1").arg(devId));
            devItem->setText(1, devId < online ? "Online" : "Offline");
            devItem->setForeground(1, devId < online ? QColor("#00ff00") : QColor("#ff6600"));

            auto deviceTags = m_dataCtrl.tagManager().getTagsByDevice(devId);
            for (quint32 tid : deviceTags) {
                auto tag = m_dataCtrl.tagManager().getTag(tid);
                if (tag.tagId == tid) {
                    auto* tagItem = new QTreeWidgetItem(devItem);
                    tagItem->setText(0, tag.tagName);
                    tagItem->setText(1, QString("Addr:%1 Reg:%2").arg(tag.modbusServerAddr).arg(tag.modbusRegAddr));
                }
            }
        }
        m_deviceTree->expandAll();
    }
}
