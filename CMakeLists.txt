cmake_minimum_required(VERSION 3.24)

project(trading-client
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)
option(TC_ENABLE_RITHMIC_API_PLUS "Build against the Rithmic R | API+ C++ SDK" OFF)
option(TC_ENABLE_KEYCHAIN_BACKUP "Also read/write the macOS Keychain credential backup" OFF)
option(TC_KEYCHAIN_ALLOW_ANY_APP "Use a rebuild-stable development Keychain ACL on macOS" ON)
set(RITHMIC_API_ROOT "" CACHE PATH "Path to the Rithmic R | API+ SDK root")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_executable(trading-client
    src/main.cpp
    src/adapters/SimulatedMarketDataAdapter.cpp
    src/core/AccountStore.cpp
    src/core/ConnectionConfig.cpp
    src/core/FeedConnectionStore.cpp
    src/core/MacKeychain.cpp
    src/ui/ConnectionTestDialog.cpp
    src/ui/ChartWindow.cpp
    src/ui/FeedSettingsDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/SelectInstrumentDialog.cpp
    src/resources.qrc
)

target_link_libraries(trading-client PRIVATE Qt6::Core Qt6::Widgets)
target_include_directories(trading-client PRIVATE src)

if(TC_ENABLE_RITHMIC_API_PLUS)
    if(NOT RITHMIC_API_ROOT)
        message(FATAL_ERROR "Set -DRITHMIC_API_ROOT=/path/to/rithmic/sdk when TC_ENABLE_RITHMIC_API_PLUS=ON")
    endif()
    target_compile_definitions(trading-client PRIVATE TC_ENABLE_RITHMIC_API_PLUS=1)
    target_include_directories(trading-client PRIVATE "${RITHMIC_API_ROOT}/include")
    target_link_directories(trading-client PRIVATE "${RITHMIC_API_ROOT}/lib")
endif()

if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    target_link_libraries(trading-client PRIVATE ${SECURITY_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    if(TC_ENABLE_KEYCHAIN_BACKUP)
        target_compile_definitions(trading-client PRIVATE TC_ENABLE_KEYCHAIN_BACKUP=1)
    endif()
    if(TC_KEYCHAIN_ALLOW_ANY_APP)
        target_compile_definitions(trading-client PRIVATE TC_KEYCHAIN_ALLOW_ANY_APP=1)
    endif()
endif()
