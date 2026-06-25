cmake_minimum_required(VERSION 3.24)

project(tick-vector
    VERSION 0.1.0
    DESCRIPTION "Native C++/Qt trading workstation for live tick data, charts, DOM, and Rithmic market connectivity"
    HOMEPAGE_URL "https://github.com/dunstorm/tick-vector"
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

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts Network WebSockets)
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
find_program(BASH_EXECUTABLE bash)

set(RITHMIC_PROTOCOL_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rithmic-protocol/proto")
set(RITHMIC_PROTOCOL_PROTO_FILES
    "${RITHMIC_PROTOCOL_PROTO_DIR}/base.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/best_bid_offer.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/forced_logout.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/last_trade.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/order_book.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/reject.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/request_heartbeat.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/request_front_month_contract.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/request_login.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/request_market_data_update.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/request_rithmic_system_info.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/request_time_bar_replay.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/response_heartbeat.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/response_front_month_contract.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/response_login.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/response_market_data_update.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/response_rithmic_system_info.proto"
    "${RITHMIC_PROTOCOL_PROTO_DIR}/response_time_bar_replay.proto"
)
protobuf_generate_cpp(RITHMIC_PROTOCOL_SRCS RITHMIC_PROTOCOL_HDRS ${RITHMIC_PROTOCOL_PROTO_FILES})

set(TICK_VECTOR_CORE_SOURCES
    src/core/AccountStore.cpp
    src/core/ConnectionConfig.cpp
    src/core/FeedConnectionStore.cpp
    src/core/MacKeychain.cpp
)

set(TICK_VECTOR_CORE_HEADERS
    src/app/AppConstants.hpp
    src/core/AccountStore.hpp
    src/core/ConnectionConfig.hpp
    src/core/FeedConnection.hpp
    src/core/FeedConnectionStore.hpp
    src/core/MacKeychain.hpp
    src/core/MarketDataAdapter.hpp
    src/core/Types.hpp
)

set(TICK_VECTOR_ADAPTER_SOURCES
    src/adapters/RithmicProtocolClient.cpp
    src/adapters/RithmicMarketDataAdapter.cpp
    src/adapters/SimulatedMarketDataAdapter.cpp
    src/adapters/TradingAdapterFactory.cpp
)

set(TICK_VECTOR_ADAPTER_HEADERS
    src/adapters/RithmicProtocolClient.hpp
    src/adapters/RithmicMarketDataAdapter.hpp
    src/adapters/SimulatedMarketDataAdapter.hpp
    src/adapters/TradingAdapterFactory.hpp
)

set(TICK_VECTOR_UI_SOURCES
    src/ui/ChartWindow.cpp
    src/ui/ConnectionTestDialog.cpp
    src/ui/DomWindow.cpp
    src/ui/FeedSettingsDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/SelectInstrumentDialog.cpp
)

set(TICK_VECTOR_UI_HEADERS
    src/ui/ChartWindow.hpp
    src/ui/ConnectionTestDialog.hpp
    src/ui/DomWindow.hpp
    src/ui/FeedSettingsDialog.hpp
    src/ui/MainWindow.hpp
    src/ui/SelectInstrumentDialog.hpp
)

add_library(tick-vector-core STATIC
    ${TICK_VECTOR_CORE_SOURCES}
    ${TICK_VECTOR_CORE_HEADERS}
)

target_include_directories(tick-vector-core PUBLIC src)
target_link_libraries(tick-vector-core PUBLIC Qt6::Core)
target_compile_features(tick-vector-core PUBLIC cxx_std_20)

add_library(tick-vector-adapters STATIC
    ${TICK_VECTOR_ADAPTER_SOURCES}
    ${TICK_VECTOR_ADAPTER_HEADERS}
    ${RITHMIC_PROTOCOL_SRCS}
    ${RITHMIC_PROTOCOL_HDRS}
)

target_include_directories(tick-vector-adapters PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tick-vector-adapters
    PUBLIC
        tick-vector-core
        Qt6::Core
    PRIVATE
        Qt6::Network
        Qt6::WebSockets
        protobuf::libprotobuf
)
target_compile_features(tick-vector-adapters PUBLIC cxx_std_20)

add_library(tick-vector-ui STATIC
    ${TICK_VECTOR_UI_SOURCES}
    ${TICK_VECTOR_UI_HEADERS}
)

target_include_directories(tick-vector-ui PUBLIC src)
target_link_libraries(tick-vector-ui
    PUBLIC
        tick-vector-core
        Qt6::Widgets
        Qt6::Charts
    PRIVATE
        tick-vector-adapters
)
target_compile_features(tick-vector-ui PUBLIC cxx_std_20)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/resources.qrc
)

target_link_libraries(${PROJECT_NAME} PRIVATE tick-vector-ui tick-vector-adapters Qt6::Widgets)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES
    src/main.cpp
    src/resources.qrc
    ${TICK_VECTOR_CORE_SOURCES}
    ${TICK_VECTOR_CORE_HEADERS}
    ${TICK_VECTOR_ADAPTER_SOURCES}
    ${TICK_VECTOR_ADAPTER_HEADERS}
    ${TICK_VECTOR_UI_SOURCES}
    ${TICK_VECTOR_UI_HEADERS}
)
source_group("Generated/Rithmic Protocol" FILES ${RITHMIC_PROTOCOL_SRCS} ${RITHMIC_PROTOCOL_HDRS})

if(BASH_EXECUTABLE)
    add_custom_target(architecture-check
        COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-architecture.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking source dependency boundaries"
    )
endif()

if(TC_ENABLE_RITHMIC_API_PLUS)
    if(NOT RITHMIC_API_ROOT)
        message(FATAL_ERROR "Set -DRITHMIC_API_ROOT=/path/to/rithmic/sdk when TC_ENABLE_RITHMIC_API_PLUS=ON")
    endif()
    target_compile_definitions(tick-vector-adapters PRIVATE TC_ENABLE_RITHMIC_API_PLUS=1)
    target_include_directories(tick-vector-adapters PRIVATE "${RITHMIC_API_ROOT}/include")
    target_link_directories(tick-vector-adapters PRIVATE "${RITHMIC_API_ROOT}/lib")
endif()

if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    target_link_libraries(tick-vector-core PRIVATE ${SECURITY_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    if(TC_ENABLE_KEYCHAIN_BACKUP)
        target_compile_definitions(tick-vector-core PRIVATE TC_ENABLE_KEYCHAIN_BACKUP=1)
    endif()
    if(TC_KEYCHAIN_ALLOW_ANY_APP)
        target_compile_definitions(tick-vector-core PRIVATE TC_KEYCHAIN_ALLOW_ANY_APP=1)
    endif()
endif()
