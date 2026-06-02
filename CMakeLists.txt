cmake_minimum_required(VERSION 3.16)

project(YAOS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

set(QT_ROOT "C:/Qt/Qt5.14.2/5.14.2/msvc2017_64" CACHE PATH "Qt 5.14.2 msvc2017_64 root")
if(EXISTS "${QT_ROOT}")
    list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Concurrent Sql Gui Qml Quick QuickControls2)

set(FASTNET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../FastNet" CACHE PATH "FastNet root")
if(NOT EXISTS "${FASTNET_ROOT}/include/FastNet/FastNet.h")
    message(FATAL_ERROR "FastNet root is invalid: ${FASTNET_ROOT}")
endif()

add_library(FastNet::FastNet UNKNOWN IMPORTED)
set_target_properties(FastNet::FastNet PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FASTNET_ROOT}/include"
    IMPORTED_LOCATION "${FASTNET_ROOT}/lib/FastNet.lib"
    IMPORTED_LOCATION_RELEASE "${FASTNET_ROOT}/lib/FastNet.lib"
    IMPORTED_LOCATION_DEBUG "${FASTNET_ROOT}/lib/FastNetd.lib"
)

function(yaos_read_pri_paths out_var pri_path)
    file(READ "${pri_path}" pri_content)
    string(REGEX MATCHALL "src/[A-Za-z0-9_./-]+\\.(cpp|h|hpp)" relative_paths "${pri_content}")
    list(REMOVE_DUPLICATES relative_paths)
    set(absolute_paths)
    foreach(relative_path IN LISTS relative_paths)
        list(APPEND absolute_paths "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}")
    endforeach()
    set(${out_var} ${absolute_paths} PARENT_SCOPE)
endfunction()

function(yaos_set_common_target_properties target_name)
    target_include_directories(${target_name} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${FASTNET_ROOT}/include"
    )
    target_compile_definitions(${target_name} PRIVATE
        $<$<CONFIG:Debug>:QT_QML_DEBUG>
    )
endfunction()

function(yaos_copy_fastnet_runtime target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<$<CONFIG:Debug>:${FASTNET_ROOT}/bin/FastNetd.dll>$<$<NOT:$<CONFIG:Debug>>:${FASTNET_ROOT}/bin/FastNet.dll>"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FASTNET_ROOT}/bin/libcrypto-3-x64.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FASTNET_ROOT}/bin/libssl-3-x64.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
    )
endfunction()

function(yaos_copy_qt_runtime target_name)
    foreach(qt_target IN LISTS ARGN)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${qt_target}>"
                "$<TARGET_FILE_DIR:${target_name}>"
        )
    endforeach()
endfunction()

yaos_read_pri_paths(YAOS_BASE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/qmake/modules/base.pri")
yaos_read_pri_paths(YAOS_BUSINESS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/qmake/modules/business.pri")
yaos_read_pri_paths(YAOS_FRONTEND_FILES "${CMAKE_CURRENT_SOURCE_DIR}/qmake/modules/frontend.pri")
yaos_read_pri_paths(YAOS_DAEMON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/qmake/modules/daemon.pri")

add_library(yaos_base STATIC ${YAOS_BASE_FILES})
yaos_set_common_target_properties(yaos_base)
target_link_libraries(yaos_base PUBLIC
    Qt5::Core
    Qt5::Concurrent
    Qt5::Sql
    FastNet::FastNet
)

add_library(yaos_business STATIC ${YAOS_BUSINESS_FILES})
yaos_set_common_target_properties(yaos_business)
target_link_libraries(yaos_business PUBLIC
    yaos_base
    Qt5::Core
    Qt5::Concurrent
    Qt5::Sql
    FastNet::FastNet
)

add_executable(yaos WIN32
    ${YAOS_FRONTEND_FILES}
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/qml.qrc"
    "${CMAKE_CURRENT_SOURCE_DIR}/yaos_resource.rc"
)
yaos_set_common_target_properties(yaos)
target_link_libraries(yaos PRIVATE
    yaos_business
    yaos_base
    Qt5::Core
    Qt5::Concurrent
    Qt5::Sql
    Qt5::Gui
    Qt5::Qml
    Qt5::Quick
    Qt5::QuickControls2
    FastNet::FastNet
)
if(WIN32)
    target_link_libraries(yaos PRIVATE user32 shell32)
endif()
yaos_copy_fastnet_runtime(yaos)
yaos_copy_qt_runtime(yaos Qt5::Core Qt5::Concurrent Qt5::Sql Qt5::Gui Qt5::Qml Qt5::Quick Qt5::QuickControls2)

add_executable(yaosd ${YAOS_DAEMON_FILES})
yaos_set_common_target_properties(yaosd)
target_link_libraries(yaosd PRIVATE
    yaos_business
    yaos_base
    Qt5::Core
    Qt5::Sql
    FastNet::FastNet
)
yaos_copy_fastnet_runtime(yaosd)
yaos_copy_qt_runtime(yaosd Qt5::Core Qt5::Sql)

add_executable(yaos_tests tests/IntegrationSmoke.cpp)
yaos_set_common_target_properties(yaos_tests)
target_compile_definitions(yaos_tests PRIVATE YAOS_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(yaos_tests PRIVATE
    yaos_business
    yaos_base
    Qt5::Core
    Qt5::Concurrent
    Qt5::Sql
    FastNet::FastNet
)
yaos_copy_fastnet_runtime(yaos_tests)
yaos_copy_qt_runtime(yaos_tests Qt5::Core Qt5::Concurrent Qt5::Sql)

enable_testing()
add_test(NAME yaos_integration_smoke COMMAND yaos_tests)
