cmake_minimum_required(VERSION 3.16)
project(DCSTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test Multimedia Sql)

# Include path to main project headers and 3rd-party
include_directories(..)
include_directories(../3pair/libmodbus-3.1.12/src)
include_directories(../3pair/qcustomplot)

# ------------------------------------------------------------------
# Test 1: DoubleBuffer (standalone, no MOC from main project needed)
# ------------------------------------------------------------------
add_executable(TestDoubleBuffer
    TestDoubleBuffer.cpp
    ../DoubleBuffer.h
    ../TagDef.h
    ../export.h
)
target_link_libraries(TestDoubleBuffer
    Qt6::Test Qt6::Core
)

# ------------------------------------------------------------------
# Test 2: AlarmEngine (needs DB, AuthManager, Logger + MOC for AlarmEngine.h)
# ------------------------------------------------------------------
add_executable(TestAlarmEngine
    TestAlarmEngine.cpp
    ../AlarmEngine.cpp
    ../AuthManager.cpp
    ../DatabaseManager.cpp
    ../logger.cpp
    # Headers listed for AUTOMOC
    ../AlarmEngine.h
    ../AuthManager.h
    ../DatabaseManager.h
    ../export.h
    ../TagDef.h
    ../LogLevelSpec.h
    ../logger.h
)
target_link_libraries(TestAlarmEngine
    Qt6::Test Qt6::Core Qt6::Multimedia Qt6::Sql
)

# ------------------------------------------------------------------
# Test 3: AuthManager (needs DB, Logger + MOC for AuthManager.h)
# ------------------------------------------------------------------
add_executable(TestAuthManager
    TestAuthManager.cpp
    ../AuthManager.cpp
    ../DatabaseManager.cpp
    ../logger.cpp
    # Headers listed for AUTOMOC
    ../AuthManager.h
    ../DatabaseManager.h
    ../export.h
    ../LogLevelSpec.h
    ../logger.h
)
target_link_libraries(TestAuthManager
    Qt6::Test Qt6::Core Qt6::Sql
)

# ------------------------------------------------------------------
# CTest support: add all tests
# ------------------------------------------------------------------
enable_testing()
add_test(NAME DoubleBuffer COMMAND TestDoubleBuffer)
add_test(NAME AlarmEngine COMMAND TestAlarmEngine)
add_test(NAME AuthManager COMMAND TestAuthManager)
