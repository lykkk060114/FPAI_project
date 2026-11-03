cmake_minimum_required(VERSION 3.16)
project(yolo26_fpga_nms_bytetrack_uart LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# ICraft 3.36 uses GNU's empty variadic-macro extension in public headers.
set(CMAKE_CXX_EXTENSIONS ON)

option(XUNJING_BUILD_FPAI_APP "Build the FMQL30TAI FPAI application" OFF)
option(XUNJING_CAPTURE_INPUT "Diagnostic build: keep last ten ImageMake frames" OFF)
option(XUNJING_PLIN_FP32_BRIDGE "Adapt signed8 ImageMake pixels to this YOLO26 FP32 input" OFF)
option(XUNJING_BUILD_OFFLINE_APP "Build host video replay with ONNX Runtime" OFF)
set(FPAI_ROOT "/work/fpai_demo_package_26040701" CACHE PATH
    "FPAI demo package containing deps/modelzoo_utils")

set(BYTETRACK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bytetrack)
add_library(bytetrack_core STATIC
    ${BYTETRACK_ROOT}/src/BYTETracker.cpp
    ${BYTETRACK_ROOT}/src/STrack.cpp
    ${BYTETRACK_ROOT}/src/kalmanFilter.cpp
    ${BYTETRACK_ROOT}/src/lapjv.cpp
    ${BYTETRACK_ROOT}/src/utils.cpp
)
target_include_directories(bytetrack_core PUBLIC ${BYTETRACK_ROOT}/include)

add_library(xunjing_tracking STATIC tracking/src/TargetTracker.cpp)
target_include_directories(xunjing_tracking PUBLIC tracking/include)
target_link_libraries(xunjing_tracking PUBLIC bytetrack_core)
target_compile_options(xunjing_tracking PRIVATE -Wall -Wextra -Wpedantic)

add_library(xunjing_detection STATIC detection/src/Yolo26Postprocess.cpp)
target_include_directories(xunjing_detection PUBLIC detection/include)
target_compile_options(xunjing_detection PRIVATE -Wall -Wextra -Wpedantic)

add_library(xunjing_ranging STATIC ranging/src/MonocularRanger.cpp)
target_include_directories(xunjing_ranging PUBLIC ranging/include)
target_compile_options(xunjing_ranging PRIVATE -Wall -Wextra -Wpedantic)

add_library(task_uart STATIC
    uart/src/Uart.cpp
    uart/src/TaskProtocol.cpp
)
target_include_directories(task_uart PUBLIC uart/include)
target_compile_options(task_uart PRIVATE -Wall -Wextra -Wpedantic)

if(XUNJING_BUILD_OFFLINE_APP AND NOT XUNJING_BUILD_FPAI_APP)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio imgcodecs)
    find_package(Eigen3 REQUIRED)
    set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime prefix with include and lib")
    find_path(ORT_INCLUDE onnxruntime_cxx_api.h HINTS ${ONNXRUNTIME_ROOT}/include REQUIRED)
    find_library(ORT_LIBRARY NAMES onnxruntime HINTS ${ONNXRUNTIME_ROOT}/lib REQUIRED)
    target_include_directories(bytetrack_core PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(bytetrack_core PUBLIC Eigen3::Eigen ${OpenCV_LIBS})
    target_include_directories(xunjing_detection PUBLIC ${OpenCV_INCLUDE_DIRS})
    add_library(xunjing_classification STATIC classification/src/BehaviorClassifier.cpp)
    target_include_directories(xunjing_classification PUBLIC classification/include ${ORT_INCLUDE} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(xunjing_classification PUBLIC ${ORT_LIBRARY} ${OpenCV_LIBS})
    add_executable(offline_pipeline app/offline_pipeline.cpp app/TaskPublisher.cpp)
    target_include_directories(offline_pipeline PRIVATE app ${ORT_INCLUDE})
    target_link_libraries(offline_pipeline PRIVATE xunjing_tracking xunjing_detection xunjing_classification task_uart ${OpenCV_LIBS})
endif()

if(XUNJING_BUILD_FPAI_APP)
    if(NOT EXISTS ${FPAI_ROOT}/deps/modelzoo_utils/include)
        message(FATAL_ERROR "FPAI_ROOT does not point to fpai_demo_package_26040701: ${FPAI_ROOT}")
    endif()
    add_compile_definitions(USE_ZG330_BACKEND)
    set(FPAI_DEPS ${FPAI_ROOT}/deps)
    set(FPAI_THIRDPARTY ${FPAI_DEPS}/thirdparty)

    target_include_directories(xunjing_detection PUBLIC
        ${FPAI_THIRDPARTY}/include
    )

    target_include_directories(bytetrack_core PUBLIC
        ${FPAI_THIRDPARTY}/include
        ${FPAI_THIRDPARTY}/include/Eigen-3.4.0
    )

    add_library(xunjing_task_publisher STATIC app/TaskPublisher.cpp)
    target_include_directories(xunjing_task_publisher PUBLIC app)
    target_link_libraries(xunjing_task_publisher PUBLIC task_uart)
    target_compile_options(xunjing_task_publisher PRIVATE -Wall -Wextra -Wpedantic)

    add_library(xunjing_classification STATIC
        classification/src/BehaviorClassifier.cpp
    )
    target_include_directories(xunjing_classification PUBLIC
        classification/include
        ${FPAI_THIRDPARTY}/include
        ${FPAI_THIRDPARTY}/include/onnxruntime
        ${FPAI_THIRDPARTY}/include/opencv2
    )
    target_link_directories(xunjing_classification PUBLIC
        ${FPAI_THIRDPARTY}/so/onnxruntime
    )
    target_link_libraries(xunjing_classification PUBLIC
        onnxruntime opencv_imgproc opencv_core
    )
    target_compile_options(xunjing_classification PRIVATE
        -Wall -Wextra -Wpedantic
    )

    link_directories(${FPAI_THIRDPARTY}/a)
    find_package(Icraft-BuyiBackend REQUIRED)
    find_package(Icraft-HostBackend REQUIRED)
    find_package(icraft-zg330backend REQUIRED)

    add_executable(sdicamera+yolo26+bytetrack+hdmi
        app/sdicamera+yolo26+bytetrack+hdmi.cpp
    )
    if(XUNJING_CAPTURE_INPUT OR XUNJING_PLIN_FP32_BRIDGE)
        # Generate an override in the build tree; leave the official SDK untouched.
        file(READ "${FPAI_DEPS}/modelzoo_utils/include/pipeline/actor/sdicamera_input_actor.hpp" CAPTURE_HEADER)
        string(REPLACE "\r\n" "\n" CAPTURE_HEADER "${CAPTURE_HEADER}")
        if(NOT CAPTURE_HEADER MATCHES "static constexpr int OPT_DEBUG_FLAGS = BIT_MASK_0;")
            message(FATAL_ERROR "Unexpected SDICameraInputActor header; review capture override")
        endif()
        if(XUNJING_CAPTURE_INPUT)
          string(REPLACE "static constexpr int OPT_DEBUG_FLAGS = BIT_MASK_0;"
            "static constexpr int OPT_DEBUG_FLAGS = BIT_MASK_0 | BIT_MASK_1;" CAPTURE_HEADER "${CAPTURE_HEADER}")
        endif()
        if(XUNJING_PLIN_FP32_BRIDGE)
            target_compile_definitions(sdicamera+yolo26+bytetrack+hdmi PRIVATE XUNJING_PLIN_FP32_BRIDGE_ENABLED)
            file(READ "${CMAKE_CURRENT_SOURCE_DIR}/app/PlinFp32Bridge.inc" BRIDGE_CODE)
            string(REPLACE "                    }\n                    catch (const std::exception &e)"
                "${BRIDGE_CODE}\n                    }\n                    catch (const std::exception &e)"
                CAPTURE_HEADER "${CAPTURE_HEADER}")
            if(NOT CAPTURE_HEADER MATCHES "PLIN bridge: signed8 BGR")
                message(FATAL_ERROR "FP32 bridge insertion failed; SDK actor changed")
            endif()
        endif()
        string(REPLACE [==[#pragma once]==] [==[#pragma once
#include "NpuExecution.hpp"]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        string(REPLACE [==[                auto &raw_chunck =]==] [==[                std::unique_lock<std::recursive_mutex> npu_access(xunjing::NpuExecutionMutex(), std::defer_lock);
                if(xunjing::DualNpuEnabled() && !xunjing::PlPipelineEnabled()) {
                    npu_access.lock();
                    if(xunjing::NpuFault().load()) break;
                    auto backend=imk_or_icore_sessions_[buffer_index]->backends[0].template cast<BackendType>();
                    if(backend->map_table.defined()){this->device_.updateMMU(backend->map_table);this->device_.clearMMUCache();}
                }
                auto &raw_chunck =]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        # New bridge: own separate legacy ImageMake scratch; start before camera take.
        string(REPLACE [==[                auto &raw_chunck = this->buffer_manager_.getChunk(chunk_group_id_, buffer_index);]==] [==[                auto &raw_chunck = this->buffer_manager_.getChunk(chunk_group_id_, buffer_index);
                thread_local std::unique_ptr<xunjing::PlInputBridge> pl_direct;
                if(xunjing::PlDirectEnabled()) {
                    if(!pl_direct) pl_direct=std::make_unique<xunjing::PlInputBridge>(this->device_);
                    pl_direct->Begin(imagemake_wddr_base_groups_[buffer_index], buffer_index);
                }]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        string(REPLACE [==[#include "NpuExecution.hpp"]==] [==[#include "NpuExecution.hpp"
#include "PlInputBridge.hpp"]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        string(REPLACE [==[write(imagemake_reg_base_ + 0x114, imagemake_wddr_base_groups_[buffer_index]);]==] [==[write(imagemake_reg_base_ + 0x114, xunjing::PlDirectEnabled() ? pl_direct->LegacyAddress() : imagemake_wddr_base_groups_[buffer_index]);]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        string(REPLACE [==[                                  imagemake_wddr_base_groups_[buffer_index], 8, empty_float_vec, empty_float_vec);]==] [==[                                  xunjing::PlDirectEnabled() ? pl_direct->LegacyAddress() : imagemake_wddr_base_groups_[buffer_index], 8, empty_float_vec, empty_float_vec);]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        string(REPLACE [==[                        runImageMakeForward(this->device_,]==] [==[                        if(!xunjing::PlDirectEnabled()) runImageMakeForward(this->device_,]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        string(REPLACE [==[                this->output_queue_->push(std::move(msg));]==] [==[                if(npu_access.owns_lock()) npu_access.unlock();
                // Detection is slower than the camera. Drop a whole frame when
                // the bounded detector queue is full, then release its buffer.
                if(!this->output_queue_->try_push(std::move(msg))) {
                    this->buffer_manager_.returnIndex(chunk_group_id_, buffer_index);
                    static unsigned long dropped_frames = 0;
                    if((++dropped_frames % 30) == 0)
                        spdlog::warn("[INPUT] detector queue full; dropped={} frames", dropped_frames);
                }]==] CAPTURE_HEADER "${CAPTURE_HEADER}")
        file(READ "${FPAI_DEPS}/modelzoo_utils/include/pipeline/base/thread_safe_queue.hpp" QUEUE_HEADER)
        string(REPLACE "    /**\n     * @brief Blocks until an item is available or stop() is called."
            "    bool try_push(T&& value) {\n        std::lock_guard<std::mutex> lock(mtx_);\n        if (stop_flag_ || q_.size() >= max_size_) return false;\n        q_.push(std::move(value)); empty_cv_.notify_one(); return true;\n    }\n\n    /**\n     * @brief Blocks until an item is available or stop() is called."
            QUEUE_HEADER "${QUEUE_HEADER}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/capture_include/pipeline/base")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/capture_include/pipeline/base/thread_safe_queue.hpp" "${QUEUE_HEADER}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/capture_include/pipeline/actor")
        string(REPLACE "#pragma once" "#pragma once\n#include \"ConsoleLogging.hpp\""
            CAPTURE_HEADER "${CAPTURE_HEADER}")
        string(REPLACE "if (OPT_DEBUG_FLAGS & BIT_MASK_0)"
            "if ((OPT_DEBUG_FLAGS & BIT_MASK_0) && xunjing::ConsoleTimingDue<xunjing::ConsoleStage::Input>())"
            CAPTURE_HEADER "${CAPTURE_HEADER}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/capture_include/pipeline/actor/sdicamera_input_actor.hpp" "${CAPTURE_HEADER}")
        file(READ "${FPAI_DEPS}/modelzoo_utils/include/icraft_utils.hpp" CAPTURE_UTILS)
        set(CAPTURE_ORIGINAL "auto fn = fmt::format(\"{}/{}_imkout_{}_{}.png\", dump_path, prefix, runBackend, get_timestamp_string());")
        string(FIND "${CAPTURE_UTILS}" "${CAPTURE_ORIGINAL}" CAPTURE_POSITION)
        if(CAPTURE_POSITION EQUAL -1)
            message(FATAL_ERROR "Unexpected SDK dump filename; review rolling capture override")
        endif()
        string(REPLACE "${CAPTURE_ORIGINAL}"
            "static unsigned int capture_slot = 0; auto fn = fmt::format(\"{}/{}_imkout_{}_{}.png\", dump_path, prefix, runBackend, capture_slot++ % 10);"
            CAPTURE_UTILS "${CAPTURE_UTILS}")
        string(REPLACE "cv::imwrite(fn, output_img);"
            "if (cv::imwrite(fn + std::string(\".pending.png\"), output_img)) std::filesystem::rename(fn + std::string(\".pending.png\"), fn);"
            CAPTURE_UTILS "${CAPTURE_UTILS}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/capture_include/icraft_utils.hpp" "${CAPTURE_UTILS}")
        target_include_directories(sdicamera+yolo26+bytetrack+hdmi BEFORE PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/capture_include")
    endif()
    # Treat a stopped/empty queue as shutdown, not a thread exception/default frame.
    # Generate local overrides; never edit the vendor SDK installation.
    foreach(ACTOR_PATH IN ITEMS ai_example/yolov5_npu_actor.hpp pipeline/actor/hdmi_display_actor.hpp)
        file(READ "${FPAI_DEPS}/modelzoo_utils/include/${ACTOR_PATH}" ACTOR_TEXT)
        string(REPLACE "\r\n" "\n" ACTOR_TEXT "${ACTOR_TEXT}")
        if(ACTOR_PATH MATCHES "yolov5_npu_actor")
            set(OLD_EXIT "throw std::runtime_error(\"IcoreActor: Failed to pop from input queue\");")
            set(NEW_EXIT "break; // Stopped and empty input queue.")
        else()
            set(OLD_EXIT "this->input_queue_->wait_and_pop(msg);")
            set(NEW_EXIT "if (!this->input_queue_->wait_and_pop(msg)) break;")
        endif()
        string(FIND "${ACTOR_TEXT}" "${OLD_EXIT}" EXIT_POSITION)
        if(EXIT_POSITION EQUAL -1)
            message(FATAL_ERROR "SDK shutdown override no longer matches ${ACTOR_PATH}")
        endif()
        string(REPLACE "${OLD_EXIT}" "${NEW_EXIT}" ACTOR_TEXT "${ACTOR_TEXT}")
        get_filename_component(ACTOR_FOLDER "${ACTOR_PATH}" DIRECTORY)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shutdown_include/${ACTOR_FOLDER}")
        if(ACTOR_PATH MATCHES "yolov5_npu_actor")
        string(REPLACE [==[#pragma once]==] [==[#pragma once
#include "NpuExecution.hpp"
#include "PlDirectNpuCheck.hpp"]==] ACTOR_TEXT "${ACTOR_TEXT}")
        string(REPLACE [==[                post_msg.icore_tensors =]==] [==[                {
                std::unique_lock<std::recursive_mutex> npu_access(xunjing::NpuExecutionMutex(), std::defer_lock);
                if(xunjing::DualNpuEnabled() || xunjing::FpgaPostV2Enabled()) {
                    npu_access.lock();
                    if(xunjing::NpuFault().load()) break;
                    auto backend=icore_session_groups_[input_msg.meta.source_id][input_msg.meta.buffer_index]->backends[0].template cast<BackendType>();
                    if(backend->map_table.defined()){this->device_.updateMMU(backend->map_table);this->device_.clearMMUCache();}
                }
                try {
                xunjing::PlDirectNpuCheck pl_npu_check(input_msg.image_tensors.at(0));
                { xunjing::StageTimer detector_forward_timer("detector_forward_and_output_wait","zg330");
                post_msg.icore_tensors =]==] ACTOR_TEXT "${ACTOR_TEXT}")
        string(REPLACE [==[                    tensor.waitForReady(std::chrono::milliseconds(10000));]==] [==[                    if(!tensor.waitForReady(std::chrono::milliseconds(10000)) && (xunjing::DualNpuEnabled() || xunjing::FpgaPostV2Enabled())) throw std::runtime_error("Detector NPU timeout");]==] ACTOR_TEXT "${ACTOR_TEXT}")
        string(REPLACE [==[                this->device_.reset(1);]==] [==[                detector_forward_timer.Success(); }
                pl_npu_check.Compare(this->device_,icore_session_groups_[input_msg.meta.source_id][input_msg.meta.buffer_index],input_msg.image_tensors,post_msg.icore_tensors);
                // Copy device outputs once while their MMU mapping is owned.
                // PS Decode/NMS then consumes host tensors without contending
                // for the next frame's capture/inference mutex.
                if(xunjing::PlPipelineEnabled() && !xunjing::FpgaPostV2Enabled()) {
                    for(auto& tensor:post_msg.icore_tensors)
                        tensor=tensor.to(icraft::xrt::HostDevice::MemRegion());
                }
                if(xunjing::FpgaPostV2Enabled()) {
                    if(!xunjing::ConsumeNativeOutputs()) throw std::runtime_error("FPGA POST consumer not initialized");
                    xunjing::ConsumeNativeOutputs()(post_msg.icore_tensors, input_msg.meta.source_id,
                        input_msg.meta.buffer_index, std::to_string(input_msg.meta.timestamp));
                }
                xunjing::ResetNpu(this->device_);
                } catch(const std::exception& e) {
                    if(!xunjing::DualNpuEnabled()) throw;
                    xunjing::NpuFault().store(true);
                    spdlog::error("Detector NPU failure: {}",e.what());
                    break;
                }
                }]==] ACTOR_TEXT "${ACTOR_TEXT}")
        endif()
        if(ACTOR_PATH MATCHES "hdmi_display_actor")
            # The display actor copies the frame into its private vector before
            # post-processing. Release the capture buffer at that point so the
            # camera is not held by NPU/post/classification latency.
            string(REPLACE [==[                auto t_read_end = std::chrono::steady_clock::now();]==]
                [==[                auto t_read_end = std::chrono::steady_clock::now();
                this->buffer_manager_.returnIndex(chunk_group_id_, msg.meta.buffer_index);]==]
                ACTOR_TEXT "${ACTOR_TEXT}")
            string(REPLACE [==[                // 4. 归还 buffer index
                this->buffer_manager_.returnIndex(chunk_group_id_, msg.meta.buffer_index);]==]
                [==[                // Buffer was released immediately after the read/copy above.]==]
                ACTOR_TEXT "${ACTOR_TEXT}")
        endif()
        string(REPLACE "#pragma once" "#pragma once\n#include \"ConsoleLogging.hpp\""
            ACTOR_TEXT "${ACTOR_TEXT}")
        if(ACTOR_PATH MATCHES "yolov5_npu_actor")
            set(TIMING_ORIGINAL [==[                LOG_INFO(LogP, "[{}][Buff{}] wait={}ms+icore={}ms",]==])
            string(FIND "${ACTOR_TEXT}" "${TIMING_ORIGINAL}" TIMING_POSITION)
            if(TIMING_POSITION EQUAL -1)
                message(FATAL_ERROR "SDK NPU timing log changed; review console throttling")
            endif()
            string(REPLACE "${TIMING_ORIGINAL}"
                "                if (xunjing::ConsoleTimingDue<xunjing::ConsoleStage::Npu>())\n${TIMING_ORIGINAL}"
                ACTOR_TEXT "${ACTOR_TEXT}")
        else()
            string(FIND "${ACTOR_TEXT}" "if (OPT_DEBUG_FLAGS & BIT_MASK_0)" TIMING_POSITION)
            if(TIMING_POSITION EQUAL -1)
                message(FATAL_ERROR "SDK display timing log changed; review console throttling")
            endif()
            string(REPLACE "if (OPT_DEBUG_FLAGS & BIT_MASK_0)"
                "if ((OPT_DEBUG_FLAGS & BIT_MASK_0) && xunjing::ConsoleTimingDue<xunjing::ConsoleStage::Display>())"
                ACTOR_TEXT "${ACTOR_TEXT}")
        endif()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shutdown_include/${ACTOR_PATH}" "${ACTOR_TEXT}")
    endforeach()
    target_include_directories(sdicamera+yolo26+bytetrack+hdmi BEFORE PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/shutdown_include")
    target_include_directories(sdicamera+yolo26+bytetrack+hdmi PRIVATE
        app
        ${FPAI_DEPS}/modelzoo_utils/include
        ${FPAI_THIRDPARTY}/include
        ${FPAI_THIRDPARTY}/include/Eigen-3.4.0
        ${FPAI_THIRDPARTY}/include/fmt
        ${FPAI_THIRDPARTY}/include/spdlog
        ${FPAI_THIRDPARTY}/include/opencv2
        ${FPAI_THIRDPARTY}/include/onnxruntime
        ${FPAI_THIRDPARTY}/include/OpenEXR
        ${FPAI_THIRDPARTY}/include/ffmpeg
    )
    target_link_libraries(sdicamera+yolo26+bytetrack+hdmi PRIVATE
        xunjing_tracking
        xunjing_detection
        xunjing_ranging
        xunjing_classification
        xunjing_task_publisher
        Icraft::HostBackend
        Icraft::BuyiBackend
        Icraft::ZG330Backend
        avformat avdevice avfilter avcodec avutil swscale swresample
        opencv_imgcodecs opencv_imgproc opencv_core tegra_hal
        libjpeg-turbo ittnotify libpng libwebp libtiff libopenjp2
        IlmImf-2_3_s Iex-2_3_s Half-2_3_s IexMath-2_3_s
        IlmImfUtil-2_3_s IlmThread-2_3_s Imath-2_3_s
        fmt pthread dl zlib yaml-cpp ssl crypto atomic stdc++fs
    )
    add_executable(classifier_zg330 app/classifier_zg330.cpp)
    get_target_property(CLASSIFIER_INCLUDES sdicamera+yolo26+bytetrack+hdmi INCLUDE_DIRECTORIES)
    get_target_property(CLASSIFIER_LIBS sdicamera+yolo26+bytetrack+hdmi LINK_LIBRARIES)
    target_include_directories(classifier_zg330 PRIVATE ${CLASSIFIER_INCLUDES})
    target_link_libraries(classifier_zg330 PRIVATE ${CLASSIFIER_LIBS})
    add_executable(pl_tasks_check app/pl_tasks_check.cpp)
    target_include_directories(pl_tasks_check PRIVATE ${CLASSIFIER_INCLUDES})
    target_link_libraries(pl_tasks_check PRIVATE ${CLASSIFIER_LIBS})
    add_executable(pl_acceptance app/pl_acceptance.cpp)
    target_include_directories(pl_acceptance PRIVATE ${CLASSIFIER_INCLUDES})
    target_link_libraries(pl_acceptance PRIVATE ${CLASSIFIER_LIBS})
    add_executable(pl_post_v2_acceptance app/pl_post_v2_acceptance.cpp)
    target_include_directories(pl_post_v2_acceptance PRIVATE ${CLASSIFIER_INCLUDES})
    target_link_libraries(pl_post_v2_acceptance PRIVATE ${CLASSIFIER_LIBS})
    add_executable(sdi_capture app/sdi_capture.cpp)
    target_include_directories(sdi_capture PRIVATE ${CLASSIFIER_INCLUDES})
    target_link_libraries(sdi_capture PRIVATE ${CLASSIFIER_LIBS})
    add_executable(detector_replay app/detector_replay.cpp)
    target_include_directories(detector_replay PRIVATE ${CLASSIFIER_INCLUDES})
    target_link_libraries(detector_replay PRIVATE ${CLASSIFIER_LIBS})
    add_executable(classifier_benchmark app/classifier_benchmark.cpp)
    target_include_directories(classifier_benchmark PRIVATE ${CLASSIFIER_INCLUDES})
    target_link_libraries(classifier_benchmark PRIVATE ${CLASSIFIER_LIBS})
    set_target_properties(sdicamera+yolo26+bytetrack+hdmi PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()
