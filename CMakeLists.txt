cmake_minimum_required(VERSION 3.22)
project(MYDSCProject_v2 VERSION 2.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network Multimedia PrintSupport)

# ---- libmodbus (C library) ----
set(LIBMODBUS_DIR ${CMAKE_SOURCE_DIR}/3pair/libmodbus-3.1.12/src)
add_library(libmodbus STATIC
    ${LIBMODBUS_DIR}/modbus.c
    ${LIBMODBUS_DIR}/modbus-tcp.c
    ${LIBMODBUS_DIR}/modbus-rtu.c
    ${LIBMODBUS_DIR}/modbus-data.c
)
target_include_directories(libmodbus PUBLIC ${LIBMODBUS_DIR})
target_compile_definitions(libmodbus PUBLIC _CRT_SECURE_NO_WARNINGS DLLBUILD)

if(WIN32)
    target_include_directories(libmodbus PRIVATE ${LIBMODBUS_DIR}/win32)
    target_compile_definitions(libmodbus PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(libmodbus PRIVATE ws2_32)
endif()

# ---- Source files ----
set(DOMAIN_SOURCES
    domain/tag/TagManager.cpp
    domain/auth/AuthManager.cpp
    domain/alarm/AlarmEngine.cpp
    domain/alarm/AlarmKpiMonitor.cpp
    domain/alarm/AlarmChangeLog.cpp
)

set(INFRA_SOURCES
    infrastructure/config/JsonConfigRepo.cpp
    infrastructure/fieldbus/ModbusComm.cpp
    infrastructure/fieldbus/ModbusImpl.cpp
    infrastructure/fieldbus/SimulatorImpl.cpp
    infrastructure/logging/SpdlogAdapter.cpp
    infrastructure/persistence/mysql/ConnectionPool.cpp
    infrastructure/persistence/mysql/AlarmMysqlRepo.cpp
    infrastructure/persistence/mysql/HistoryMysqlRepo.cpp
    infrastructure/persistence/mysql/TagMysqlRepo.cpp
    infrastructure/persistence/mysql/UserMysqlRepo.cpp
    infrastructure/persistence/mysql/OperationMysqlRepo.cpp
)

set(PIPELINE_SOURCES
    pipeline/DataParseThread.cpp
    pipeline/HistorySampler.cpp
    pipeline/DataPipeline.cpp
)

set(APPLICATION_SOURCES
    application/DataController.cpp
    application/AlarmController.cpp
    application/AuthController.cpp
)

set(PRESENTATION_SOURCES
    presentation/MainWindow.cpp
    presentation/PidScene.cpp
    presentation/TrendWidget.cpp
    presentation/TagConfigDialog.cpp
    presentation/widgets/BaseGraphicsItem.cpp
    presentation/widgets/TankItem.cpp
    presentation/widgets/ValveItem.cpp
    presentation/widgets/PumpItem.cpp
    presentation/widgets/PipeItem.cpp
    presentation/widgets/DataLabelItem.cpp
)

# ---- QCustomPlot (needs big-obj on MinGW) ----
add_library(qcustomplot STATIC 3pair/qcustomplot/qcustomplot.cpp)
target_include_directories(qcustomplot PUBLIC ${CMAKE_SOURCE_DIR}/3pair/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)
if(MINGW)
    target_compile_options(qcustomplot PRIVATE -Wa,-mbig-obj)
endif()

set(ALL_SOURCES
    main.cpp
    ${DOMAIN_SOURCES}
    ${INFRA_SOURCES}
    ${PIPELINE_SOURCES}
    ${APPLICATION_SOURCES}
    ${PRESENTATION_SOURCES}
)

set(ALL_HEADERS
    app/AppConfig.h
    app/AppContext.h
    app/ApplicationBuilder.h
    domain/common/AlarmLimit.h
    domain/common/AlarmState.h
    domain/common/DataQuality.h
    domain/common/Enums.h
    domain/tag/TagInfo.h
    domain/tag/TagManager.h
    domain/tag/DeadbandFilter.h
    domain/tag/DeviationChecker.h
    domain/tag/RateOfChangeChecker.h
    domain/auth/User.h
    domain/auth/PasswordHasher.h
    domain/auth/SessionManager.h
    domain/auth/AuthManager.h
    domain/alarm/AlarmEvent.h
    domain/alarm/AlarmStateMachine.h
    domain/alarm/ChatteringGuard.h
    domain/alarm/FloodDetector.h
    domain/alarm/ShelveManager.h
    domain/alarm/SuppressionEngine.h
    domain/alarm/AlarmEngine.h
    domain/alarm/AlarmKpiMonitor.h
    domain/alarm/AlarmChangeLog.h
    infrastructure/config/IConfigRepo.h
    infrastructure/config/JsonConfigRepo.h
    infrastructure/fieldbus/IFieldbus.h
    infrastructure/fieldbus/ModbusComm.h
    infrastructure/fieldbus/ModbusImpl.h
    infrastructure/fieldbus/SimulatorImpl.h
    infrastructure/fieldbus/OpcUaImpl.h
    infrastructure/logging/ILogger.h
    infrastructure/logging/SpdlogAdapter.h
    infrastructure/messaging/IMessageBus.h
    infrastructure/messaging/LockFreeRingBuffer.h
    infrastructure/messaging/DoubleBuffer.h
    infrastructure/persistence/IAlarmRepo.h
    infrastructure/persistence/IHistoryRepo.h
    infrastructure/persistence/ITagRepo.h
    infrastructure/persistence/IUserRepo.h
    infrastructure/persistence/IOperationRepo.h
    infrastructure/persistence/mysql/ConnectionPool.h
    infrastructure/persistence/mysql/AlarmMysqlRepo.h
    infrastructure/persistence/mysql/HistoryMysqlRepo.h
    infrastructure/persistence/mysql/TagMysqlRepo.h
    infrastructure/persistence/mysql/UserMysqlRepo.h
    infrastructure/persistence/mysql/OperationMysqlRepo.h
    infrastructure/persistence/sqlite/AlarmSqliteRepo.h
    infrastructure/threading/ThreadGuard.h
    pipeline/DataParseThread.h
    pipeline/HistorySampler.h
    pipeline/DataPipeline.h
    application/DataController.h
    application/AlarmController.h
    application/AuthController.h
    presentation/MainWindow.h
    presentation/PidScene.h
    presentation/TrendWidget.h
    presentation/TagConfigDialog.h
    presentation/widgets/BaseGraphicsItem.h
    presentation/widgets/TankItem.h
    presentation/widgets/ValveItem.h
    presentation/widgets/PumpItem.h
    presentation/widgets/PipeItem.h
    presentation/widgets/DataLabelItem.h
)

# ---- Executable ----
add_executable(${PROJECT_NAME} ${ALL_SOURCES} ${ALL_HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Multimedia
    libmodbus
    qcustomplot
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
)

# Copy config and data directories to build dir
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:${PROJECT_NAME}>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:${PROJECT_NAME}>/data
)
