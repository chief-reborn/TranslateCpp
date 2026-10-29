cmake_minimum_required(VERSION 3.25)

project(TranslateCpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 单文件分发：MSVC 也链静态运行库，用户不必安装 VC++ 运行库。
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 优先使用系统里已有的 nlohmann/json，找不到再联网下载。
find_package(nlohmann_json 3.12 QUIET)

if(NOT nlohmann_json_FOUND)
    include(FetchContent)

    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    FetchContent_MakeAvailable(nlohmann_json)
endif()

# 图标、版本信息和 DPI 清单都通过资源文件编进 exe。
set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
set(APP_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/TranslateCpp.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TranslateCpp.rc"
    @ONLY
)

add_executable(TranslateCpp WIN32
    src/main.cpp
    src/Config.cpp
    src/GoogleTranslator.cpp
    src/TextConverter.cpp
    src/PopupWindow.cpp
    src/TrayIcon.cpp
    src/StartupManager.cpp
    src/Vocabulary.cpp
    src/VocabularyWindow.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/TranslateCpp.rc"
)

target_include_directories(TranslateCpp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(TranslateCpp PRIVATE
    TRANSLATECPP_VERSION="${PROJECT_VERSION}"
    # 防止 windows.h 的 min/max 宏把 std::min/std::max 展开坏（MSVC 下会报 C2589）
    NOMINMAX
    # 让 commctrl 等通用控件的宏走宽字符版本
    UNICODE
    _UNICODE
)

target_link_libraries(
    TranslateCpp
    PRIVATE
    nlohmann_json::nlohmann_json
    winhttp
    shell32
    comctl32
)

if(MSVC)
    target_compile_options(TranslateCpp PRIVATE
        /W4
        /permissive-
        /utf-8
    )

    # manifest 已经由 .rc 嵌入，链接器不要再生成一份，否则 CVT1100 重复资源
    target_link_options(TranslateCpp PRIVATE /MANIFEST:NO)
else()
    target_compile_options(TranslateCpp PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# 静态链接 GCC 运行库，避免别人机器上缺 libstdc++-6.dll。
if(MINGW)
    target_link_options(TranslateCpp PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

install(TARGETS TranslateCpp RUNTIME DESTINATION .)

install(FILES
    README.md
    LICENSE
    config.example.json
    DESTINATION .
)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "TranslateCpp-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_VENDOR "TranslateCpp")

include(CPack)
