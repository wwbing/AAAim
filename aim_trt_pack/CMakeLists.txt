cmake_minimum_required(VERSION 3.16)
project(aim_trt_pack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(DEFINED ENV{VCPKG_ROOT})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
endif()
if(EXISTS "D:/vcpkg/installed/x64-windows/share/imgui/imgui-config.cmake")
    list(APPEND CMAKE_PREFIX_PATH "D:/vcpkg/installed/x64-windows")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui dnn)
find_package(imgui CONFIG REQUIRED)
get_filename_component(OpenCV_INSTALL_ROOT "${OpenCV_DIR}/../.." ABSOLUTE)

set(ONNXRUNTIME_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-win-x64-gpu-1.23.2"
    CACHE PATH "Path to ONNX Runtime prebuilt package root")

set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_LIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")

if(NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime headers not found: ${ONNXRUNTIME_INCLUDE_DIR}")
endif()
if(NOT EXISTS "${ONNXRUNTIME_LIB}")
    message(FATAL_ERROR "ONNX Runtime import library not found: ${ONNXRUNTIME_LIB}")
endif()

add_executable(aim_stable
    src/app_main.cpp
    src/ort_trt_infer.cpp
    src/yolo_decoder.cpp
    src/aim_control.cpp
    src/runtime_helpers.cpp
    src/target_selector.cpp
    src/preview_renderer.cpp
    src/perf_logger.cpp
    src/control_panel.cpp
)

if(MSVC)
    target_compile_options(aim_stable PRIVATE /utf-8)
endif()

target_include_directories(aim_stable PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(aim_stable PRIVATE
    ${OpenCV_LIBS}
    ${ONNXRUNTIME_LIB}
    imgui::imgui
)

if(WIN32)
    target_link_libraries(aim_stable PRIVATE
        d3d11
        dxgi
        imm32
    )

    add_custom_command(TARGET aim_stable POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/CS2_1_CLS_Sim.onnx"
            "$<TARGET_FILE_DIR:aim_stable>/CS2_1_CLS_Sim.onnx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ddll64.dll"
            "$<TARGET_FILE_DIR:aim_stable>/ddll64.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ONNXRUNTIME_ROOT}/lib"
            "$<TARGET_FILE_DIR:aim_stable>"
    )

    string(TOLOWER "${CMAKE_BUILD_TYPE}" AIM_BUILD_TYPE_LOWER)
    if(AIM_BUILD_TYPE_LOWER STREQUAL "debug")
        if(EXISTS "${OpenCV_INSTALL_ROOT}/debug/bin")
            add_custom_command(TARGET aim_stable POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${OpenCV_INSTALL_ROOT}/debug/bin"
                    "$<TARGET_FILE_DIR:aim_stable>"
            )
        endif()
    else()
        if(EXISTS "${OpenCV_INSTALL_ROOT}/bin")
            add_custom_command(TARGET aim_stable POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${OpenCV_INSTALL_ROOT}/bin"
                    "$<TARGET_FILE_DIR:aim_stable>"
            )
        endif()
    endif()
endif()
