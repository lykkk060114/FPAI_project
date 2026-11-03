#include "Link001InputEvidence.hpp"
#include "PlInputBridge.hpp"
#include "PlPostV2.hpp"
// application includes
#include "ai_example/postprocesses.hpp"
#include "ai_example/yolov5_npu_actor.hpp"
#include "TaskPublisher.hpp"
#include "TrackOverlay.hpp"
#include "FrameReferenceCapture.hpp"
#include "FrameTimingCsv.hpp"
#include "PlinInputFormat.hpp"
#include "classification/AsyncBehaviorClassifier.hpp"
#include "detection/Yolo26Postprocess.hpp"
#include "ranging/MonocularRanger.hpp"
#include "tracking/TargetTracker.hpp"

// pipeline includes
#include "pipeline/actor/message_router.hpp"
#include "pipeline/actor/sdicamera_input_actor.hpp"
#include "pipeline/actor/hdmi_display_actor.hpp"
#include "pipeline/io/input/sdicamera.hpp"
#include "pipeline/memory/buffer_manager.hpp"
// modelzoo_utils includes
#include "file_utils.hpp"
#include "et_device.hpp"
#include "fps_calculator.hpp"
#include "vis_helper.hpp"
#include "icraft_utils.hpp"
#include "ZgClassification.hpp"
// icraft includes
#include <icraft-xrt/core/session.h>
#include "compile_fpai_target.hpp"
// 3rd-party dependency includes
#include <opencv2/opencv.hpp>
#include "yaml-cpp/yaml.h"
// system libs
#include <csignal>
#include <poll.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void requestStop(int) { stop_requested = 1; }
}

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <functional>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <map>
#include <tuple>

using namespace icraft::xrt;
using namespace icraft::xir;
using namespace std::chrono;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

using namespace fpai;

const std::string DEMO_NAME = "sdicamera+yolo26+bytetrack+hdmi";

const size_t BUFFER_COUNT = 4;
const size_t CAMERA_COUNT = 1;

constexpr bool SAVE = false; // 是否保存推理结果到文件，保存格式为: <class_id> <left> <top> <width> <height> <confidence>
constexpr bool DRAW = true; // 是否在输出上绘制检测框

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config yaml>" << std::endl;
        return 1;
    }
    // ----------------- Load system configuration from YAML file -----------------
    YAML::Node config;
    try
    {
        config = YAML::LoadFile(argv[1]);
    }
    catch (const YAML::BadFile &e)
    {
        spdlog::error("Failed to load config file {}: {}", argv[1], e.what());
        return -1;
    }

    // ----------------- 设置 spdlog 日志级别（默认级别为 info） -----------------
    auto log_level = spdlog::level::info;
    spdlog::set_level(log_level);
    spdlog::info("Log level set to '{}'", spdlog::level::to_string_view(log_level));
    // --- 日志级别设置完毕 ---

    // ---------------------------- 加载各个Pipeline的配置 -----------------------------------
    const auto &sys_yaml = config["sys"];
    const auto &icore_yaml = config["pipeline"]["icore"];
    const auto &display_yaml = config["pipeline"]["display"];
    const auto &camera_yaml = config["pipeline"]["camera"];
    const auto tracker_yaml = config["pipeline"]["tracker"];
    const auto uart_yaml = config["pipeline"]["uart"];
    const auto classifier_yaml = config["pipeline"]["classification"];
    const auto ranging_yaml = config["pipeline"]["ranging"];
    const auto input_yaml = config["pipeline"]["input"];
    const std::string POST_BACKEND = icore_yaml && icore_yaml["post_backend"]
        ? icore_yaml["post_backend"].as<std::string>() : "cpu";
    const bool POST_CPU_FALLBACK = icore_yaml && icore_yaml["post_cpu_fallback"]
        ? icore_yaml["post_cpu_fallback"].as<bool>() : true;
    if (POST_BACKEND != "cpu" && POST_BACKEND != "fpga_v2")
        throw std::runtime_error("Unknown YOLO26 post_backend: " + POST_BACKEND);
    xunjing::FpgaPostV2Enabled() = POST_BACKEND == "fpga_v2";
    const auto input_mode = input_yaml && input_yaml["mode"] ? input_yaml["mode"].as<std::string>() : "legacy_cpu";
    if(input_mode!="pl_direct_verified" && input_mode!="legacy_cpu") throw std::runtime_error("Unknown input mode");
    xunjing::PlDirectEnabled() = input_mode=="pl_direct_verified";
    xunjing::PlPipelineEnabled() = xunjing::PlDirectEnabled() && input_yaml["pipelined"] && input_yaml["pipelined"].as<bool>();
    if(input_yaml && input_yaml["verify_every_frames"]) xunjing::PlVerifyInterval()=input_yaml["verify_every_frames"].as<unsigned>();
    spdlog::info("Detection input: {}, pipelined={}, verify_every_frames={}, post_backend={}, cpu_fallback={}",
                 input_mode,xunjing::PlPipelineEnabled(),xunjing::PlVerifyInterval(),POST_BACKEND,POST_CPU_FALLBACK);
    // Parse system configuration
    FPAIConfig fpai_cfg;
    fpai_cfg.device_url = sys_yaml["device"].as<std::string>();
    fpai_cfg.speed_mode = sys_yaml["speedMode"] ? sys_yaml["speedMode"].as<bool>() : false;
    fpai_cfg.compress_ftmp = sys_yaml["compressFtmp"] ? sys_yaml["compressFtmp"].as<bool>() : false;
    fpai_cfg.mmu_mode = sys_yaml["mmuMode"] ? sys_yaml["mmuMode"].as<bool>() : true;
    fpai_cfg.ocm_option = sys_yaml["ocm_option"] ? sys_yaml["ocm_option"].as<int>() : 4;
    fpai_cfg.run_backend = sys_yaml["run_backend"] ? sys_yaml["run_backend"].as<std::string>() : "buyi";
    fpai_cfg.enable_profile = sys_yaml["profile"] ? sys_yaml["profile"].as<bool>() : false;

    // Parse PLin configuration
    const int CAMERA_W = camera_yaml["width"].as<int>();
    const int CAMERA_H = camera_yaml["height"].as<int>();
    const int CAMERA_FPS = camera_yaml["fps"].as<int>();
    const bool VTC_ON = camera_yaml["vtc"] ? camera_yaml["vtc"].as<bool>() : false;

    // Parse encoder configuration
    const int FRAME_W = display_yaml["width"].as<int>();
    const int FRAME_H = display_yaml["height"].as<int>(); // PLin SDICamera推荐使用UYVY

    // Parse icore configuration
    Yolov5Config det_cfg;
    if (icore_yaml["conf"])
        det_cfg.CONF = icore_yaml["conf"].as<float>();
    if (icore_yaml["iou_thresh"])
        det_cfg.IOU_THRESHOLD = icore_yaml["iou_thresh"].as<float>();
    if (icore_yaml["multilabel"])
        det_cfg.MULTILABEL = icore_yaml["multilabel"].as<bool>();
    if (icore_yaml["names"])
        det_cfg.NAMES_PATH = icore_yaml["names"].as<std::string>();
    if (fs::exists(det_cfg.NAMES_PATH))
    {
        det_cfg.LABELS = toVector(det_cfg.NAMES_PATH);
        det_cfg.N_CLASS = det_cfg.LABELS.size();
        spdlog::info("Loaded {} class names from {}", det_cfg.N_CLASS, det_cfg.NAMES_PATH);
    }
    else
    {
        spdlog::warn("Names file {} does not exist. No class names loaded.", det_cfg.NAMES_PATH);
    }
    if (icore_yaml["number_of_class"])
        det_cfg.N_CLASS = icore_yaml["number_of_class"].as<int>();
    if (icore_yaml["fpga_nms"])
        det_cfg.FPGA_NMS = icore_yaml["fpga_nms"].as<bool>();
    if (icore_yaml["detpost"])
        det_cfg.DETPOST = icore_yaml["detpost"].as<bool>();
    if (icore_yaml["net_w"])
        det_cfg.NET_W = icore_yaml["net_w"].as<int>();
    if (icore_yaml["net_h"])
        det_cfg.NET_H = icore_yaml["net_h"].as<int>();
    det_cfg.FRAME_W = FRAME_W;
    det_cfg.FRAME_H = FRAME_H;
    if (icore_yaml["jsons"])
        read_node_urls(icore_yaml, "jsons", det_cfg.JSON_PATHS);
    if (icore_yaml["raws"])
        read_node_urls(icore_yaml, "raws", det_cfg.RAW_PATHS);
    if (icore_yaml["anchors"])
        det_cfg.ANCHORS = icore_yaml["anchors"].as<std::vector<std::vector<std::vector<float>>>>();
    det_cfg.view_except_op_ids = icore_yaml["view_except_op_ids"] ? icore_yaml["view_except_op_ids"].as<std::vector<int64_t>>() : det_cfg.view_except_op_ids;

    const std::string MODEL_FAMILY = icore_yaml["model_family"]
                                         ? icore_yaml["model_family"].as<std::string>()
                                         : "yolo26";
    const float YOLO26_OUTPUT_SCALE = icore_yaml["output_scale"]
                                          ? icore_yaml["output_scale"].as<float>()
                                          : 1.0F;
    if (MODEL_FAMILY != "yolo26")
    {
        spdlog::error("This executable is YOLO26-only; model_family '{}' is unsupported", MODEL_FAMILY);
        return -1;
    }

    const bool TRACKER_ENABLED = tracker_yaml && tracker_yaml["enabled"]
                                     ? tracker_yaml["enabled"].as<bool>()
                                     : true;
    const std::string TARGET_CLASS = tracker_yaml && tracker_yaml["target_class"]
                                         ? tracker_yaml["target_class"].as<std::string>()
                                         : "person";
    const int TRACK_BUFFER = tracker_yaml && tracker_yaml["track_buffer"]
                                 ? tracker_yaml["track_buffer"].as<int>()
                                 : 15;
    const auto target_class_it = std::find(det_cfg.LABELS.begin(),
                                           det_cfg.LABELS.end(), TARGET_CLASS);
    if (TRACKER_ENABLED && target_class_it == det_cfg.LABELS.end())
    {
        spdlog::error("Tracker target class '{}' is absent from {}",
                      TARGET_CLASS, det_cfg.NAMES_PATH);
        return -1;
    }
    const int TARGET_CLASS_ID = TRACKER_ENABLED
                                    ? static_cast<int>(std::distance(det_cfg.LABELS.begin(), target_class_it))
                                    : -1;

    xunjing::UartPublisherConfig uart_config;
    uart_config.enabled = uart_yaml && uart_yaml["enabled"]
                              ? uart_yaml["enabled"].as<bool>()
                              : false;
    if (uart_yaml && uart_yaml["device"])
        uart_config.device = uart_yaml["device"].as<std::string>();
    if (uart_yaml && uart_yaml["baud"])
        uart_config.baud = uart_yaml["baud"].as<int>();
    if (uart_yaml && uart_yaml["max_send_hz"])
        uart_config.max_send_hz = uart_yaml["max_send_hz"].as<int>();

    xunjing::classification::ClassifierConfig classifier_config;
    classifier_config.enabled = classifier_yaml && classifier_yaml["enabled"]
                                    ? classifier_yaml["enabled"].as<bool>()
                                    : false;
    if (classifier_yaml && classifier_yaml["model"])
        classifier_config.model_path = classifier_yaml["model"].as<std::string>();
    if (classifier_yaml && classifier_yaml["labels"])
    {
        const auto label_path = classifier_yaml["labels"].as<std::string>();
        if (fs::exists(label_path))
            classifier_config.labels = toVector(label_path);
        else if (classifier_config.enabled)
        {
            spdlog::error("Classifier labels file does not exist: {}", label_path);
            return -1;
        }
    }
    if (classifier_yaml && classifier_yaml["input_width"])
        classifier_config.input_width = classifier_yaml["input_width"].as<int>();
    if (classifier_yaml && classifier_yaml["input_height"])
        classifier_config.input_height = classifier_yaml["input_height"].as<int>();
    if (classifier_yaml && classifier_yaml["interval_frames"])
        classifier_config.interval_frames = classifier_yaml["interval_frames"].as<int>();
    if (classifier_yaml && classifier_yaml["threads"])
        classifier_config.intra_op_threads = classifier_yaml["threads"].as<int>();
    if (classifier_yaml && classifier_yaml["confidence"])
        classifier_config.confidence_threshold = classifier_yaml["confidence"].as<float>();
    if (classifier_yaml && classifier_yaml["smoothing_alpha"])
        classifier_config.smoothing_alpha = classifier_yaml["smoothing_alpha"].as<float>();
    if (classifier_yaml && classifier_yaml["roi_padding"])
        classifier_config.roi_padding = classifier_yaml["roi_padding"].as<float>();

    xunjing::ranging::RangerConfig ranger_config;
    if (ranging_yaml && ranging_yaml["test_fixed_distance_m"])
        throw std::runtime_error("Fixed UART distance is no longer supported; use calibrated ranging");
    ranger_config.enabled = ranging_yaml && ranging_yaml["enabled"]
                                ? ranging_yaml["enabled"].as<bool>()
                                : false;
    if (ranging_yaml && ranging_yaml["scale_m_px"])
        ranger_config.scale_m_px = ranging_yaml["scale_m_px"].as<float>();
    if (ranging_yaml && ranging_yaml["bias_m"])
        ranger_config.bias_m = ranging_yaml["bias_m"].as<float>();
    if (ranging_yaml && ranging_yaml["min_distance_m"])
        ranger_config.min_distance_m = ranging_yaml["min_distance_m"].as<float>();
    if (ranging_yaml && ranging_yaml["max_distance_m"])
        ranger_config.max_distance_m = ranging_yaml["max_distance_m"].as<float>();
    if (ranging_yaml && ranging_yaml["smoothing_alpha"])
        ranger_config.smoothing_alpha = ranging_yaml["smoothing_alpha"].as<float>();

    spdlog::info("ByteTrack: enabled={}, target_class={} (id={}), track_buffer={}",
                 TRACKER_ENABLED, TARGET_CLASS, TARGET_CLASS_ID, TRACK_BUFFER);
    spdlog::info("UART: enabled={}, device={}, baud={}, max_send_hz={}",
                 uart_config.enabled, uart_config.device, uart_config.baud,
                 uart_config.max_send_hz);
    spdlog::info("Model family: {}", MODEL_FAMILY);
    spdlog::info("Classification: enabled={}, model={}, interval_frames={}",
                 classifier_config.enabled, classifier_config.model_path,
                 classifier_config.interval_frames);
    spdlog::info("Ranging: enabled={}, calibrated={}, scale_m_px={}, bias_m={}",
                 ranger_config.enabled, ranger_config.scale_m_px > 0.0F,
                 ranger_config.scale_m_px, ranger_config.bias_m);

    std::tuple<int, int, int, int> ratio_bias;
    int RATIO_W = CAMERA_W / det_cfg.NET_W;
    int RATIO_H = CAMERA_H / det_cfg.NET_H;
    int IMG_W = RATIO_W * det_cfg.NET_W;
    int IMG_H = RATIO_H * det_cfg.NET_H;
    int BIAS_W = (FRAME_W - IMG_W) / 2;
    int BIAS_H = (FRAME_H - IMG_H) / 2;
    ratio_bias = std::make_tuple(RATIO_W, BIAS_W, RATIO_H, BIAS_H);

    // ---------------------------- 正式开始配置pipeline和加载网络 -----------------------------------
#ifndef XUNJING_PLIN_FP32_BRIDGE_ENABLED
    throw std::runtime_error("YOLO26 FP32 input is incompatible with signed8 ImageMake: rebuild with XUNJING_PLIN_FP32_BRIDGE=ON");
#else
    if (xunjing::PlDirectEnabled())
        spdlog::info("PL direct FP32 input enabled; legacy CPU input conversion bypassed");
    else
        spdlog::warn("Using legacy signed8-to-FP32 CPU input bridge");
#endif
    // 1. FPAI设备初始化
    auto device = Device::Open(fpai_cfg.device_url.c_str());
    auto fpai_device = device.cast<FPAIDevice>();
    auto post_regs = device.defaultRegRegion();
    xunjing::postv2::Controller<decltype(post_regs)> post_controller(post_regs);
    decltype(device.getMemRegion("udma").malloc(512, false)) post_result_mem;
    std::uint32_t post_result_addr = 0;
    std::ofstream post_timing;
    if (POST_BACKEND == "fpga_v2")
    {
        post_controller.requireReady();
        post_result_mem = device.getMemRegion("udma").malloc(512, false);
        post_result_addr = static_cast<std::uint32_t>((post_result_mem->begin.addr() + 63ULL) & ~63ULL);
        if (static_cast<uint64_t>(post_result_addr) + 448 > 0x40000000ULL)
            throw std::runtime_error("FPGA POST result buffer is outside PS-visible address range");
        std::vector<char> zero(448, 0);
        post_result_mem.write(post_result_addr - post_result_mem->begin.addr(), zero.data(), zero.size());
        std::filesystem::create_directories("results");
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        post_timing.open("results/fpga_post_v2_" + std::to_string(stamp) + ".csv");
        if (!post_timing)
            throw std::runtime_error("Cannot open FPGA POST timing CSV");
        post_timing << "frame,count,status,wait_ms,total_ms,backend,source_id,buffer_index,source_timestamp,steady_ns\n";
        spdlog::info("FPGA POST V2 ready: id=0x{:08x} layout=0x{:08x} result=0x{:08x}",
                     post_controller.id(), post_controller.layout(), post_result_addr);
    }
#if defined(USE_BUYI_BACKEND)
    fpai_device.mmuModeSwitch(fpai_cfg.mmu_mode); // 关闭MMU
#endif
    // 多个摄像头输入，多个寄存器组
    std::vector<uint64_t> sdicamera_base_addr_group = {
        0x40080000,
        0x40080400,
        0x40080800,
        0x40080C00};
    std::vector<uint64_t> image_make_base_addr_group = {
        0x80000400,
        0x80040000,
        0x80040400,
        0x80040800};

    // 2. 构建 ICORE Actor 的输入、输出队列

    ThreadSafeQueue<InputMessageForIcore> icore_input_queue(BUFFER_COUNT); // SDI Camera -> Icore
    ThreadSafeQueue<IcoreMessageForPost> icore_output_queue(BUFFER_COUNT); // Icore -> HDMI Display

    // 3. 使用 std::vector 存储 Actors 和相关对象
    // XXXActor 这类包含线程、锁或引用的对象是不可移动、不可拷贝的。
    // 不能将它们的对象本身直接存储在 std::vector 中, 必须将它们也改为存储 std::unique_ptr
    std::unique_ptr<SDICameraInputActor<FPAIDevice, FPAIBackend>> camera_actor;

    BufferManager buffer_manager(BUFFER_COUNT);

    // 6. 配置NPU Actor
    int icore_id = 0;
    MultiYolov5IcoreActor<FPAIDevice, FPAIBackend> icore_actor(icore_id,
                                                               fpai_device,
                                                               buffer_manager,
                                                               CAMERA_COUNT,
                                                               det_cfg,
                                                               fpai_cfg);
    icore_actor.bindInputQueue(&icore_input_queue);
    icore_actor.bindOutputQueue(&icore_output_queue);
    // ----------------- 为每个流创建独立的后处理状态 -----------------
    // Define the YOLO26 post-processing function as a lambda.
    std::vector<YoloPostResult> last_results_group(CAMERA_COUNT); // Store last results for each camera
    auto netinfos = icore_actor.getNetInfos();
    std::vector<FPSCalculator> fps_calculators(CAMERA_COUNT); // 为每个流创建一个FPS计算器
    xunjing::tracking::TargetTracker target_tracker({
        CAMERA_FPS, TRACK_BUFFER, FRAME_W, FRAME_H});
    xunjing::TaskPublisher task_publisher(uart_config);
    std::shared_ptr<ZgClassification> zg_classifier;
    const auto classifier_backend = classifier_yaml && classifier_yaml["backend"]
        ? classifier_yaml["backend"].as<std::string>() : "cpu";
    if (classifier_config.enabled && classifier_backend == "zg330") {
        if(classifier_config.input_width!=224 || classifier_config.input_height!=224 ||
           classifier_config.labels!=std::vector<std::string>{"drink","eat","rest","walk"})
            throw std::runtime_error("ZG classifier dimensions/label order mismatch");
        zg_classifier = std::make_shared<ZgClassification>(device,
            classifier_yaml["json"].as<std::string>(), classifier_yaml["raw"].as<std::string>());
        classifier_config.infer_logits = [zg_classifier](const auto& input) { return zg_classifier->Run(input); };
        xunjing::DualNpuEnabled() = true;
    } else if(classifier_config.enabled && classifier_backend != "cpu") throw std::runtime_error("Unknown classifier backend");
    if(xunjing::PlDirectEnabled()) xunjing::DualNpuEnabled()=true; // serialize capture against SDK NPU reset/MMU changes
    xunjing::classification::AsyncBehaviorClassifier behavior_classifier(classifier_config);
    xunjing::ranging::MonocularRanger monocular_ranger(ranger_config);
    if (classifier_config.enabled && !behavior_classifier.Ready())
    {
        spdlog::warn("Behavior classifier unavailable: {}",
                     behavior_classifier.InitialError());
    }

    // 7. 配置单路流水线的输入和输出
    int i = 0;
    // 配置摄像头输入，为每个编码器创建独立的配置副本
    // 配置摄像头
    int camera_id = i;
    auto camera = std::make_unique<GenericSDICamera>(camera_id, fpai_device, CAMERA_W, CAMERA_H, camera_fmt::RGB565,
                                                     FRAME_W, FRAME_H, camera_fmt::RGB565, crop_position::center, // RGB565格式用于hdmi显示
                                                     det_cfg.NET_W, det_cfg.NET_H, crop_position::center,   // PL端输入必须经过hardResizePL
                                                     sdicamera_base_addr_group[i], VTC_ON);
#if defined(USE_BUYI_BACKEND)
    camera_actor = std::make_unique<SDICameraInputActor<FPAIDevice, FPAIBackend>>(
        camera->getSourceId(),
        std::move(camera),
        fpai_device,
        buffer_manager,
        icore_actor.getImkSessionGroups()[i],
        image_make_base_addr_group[i]);
#elif defined(USE_ZG330_BACKEND)
    camera_actor = std::make_unique<SDICameraInputActor<FPAIDevice, FPAIBackend>>(
        camera->getSourceId(),
        std::move(camera),
        fpai_device,
        buffer_manager,
        icore_actor.getIcoreSessionGroups()[i], // pass ref of whole icore sessions
        image_make_base_addr_group[i]);
#endif
    camera_actor->bindOutputQueue(&icore_input_queue);

    xunjing::TrackOverlay track_overlay;
    xunjing::FrameTimingCsv timing_csv;
    std::uint64_t console_frames = 0;
    std::array<double, 8> console_stage_ms{};
    double console_fpga_post_ms = 0.0;
    auto console_window_start = std::chrono::steady_clock::now();
    std::uint32_t fpga_post_frame = 0;

    struct PostObservation {
        xunjing::postv2::Result result{};
        double wait_ms = 0.0;
        uint32_t status = 0;
        bool success = false;
    };
    using PostKey = std::tuple<int, int, std::string>;
    std::mutex post_observations_mutex;
    std::map<PostKey, PostObservation> post_observations;
    if (POST_BACKEND == "fpga_v2")
    {
        xunjing::ConsumeNativeOutputs() = [&](std::vector<icraft::xrt::Tensor>& outputs,
            int source, int buffer, const std::string& timestamp) {
            PostObservation observation;
            const auto begin = std::chrono::steady_clock::now();
            try {
                xunjing::StageTimer stage("fpga_decode_nms", "fpga", fpga_post_frame + 1);
                if (outputs.size() != 6) throw std::runtime_error("FPGA POST expects six outputs");
                static constexpr int shapes[6][4] = {{1,44,80,1},{1,44,80,4},{1,22,40,1},{1,22,40,4},{1,11,20,1},{1,11,20,4}};
                static constexpr uint32_t bytes[6] = {14080,56320,3520,14080,880,3520};
                xunjing::postv2::Job job;
                job.frame = ++fpga_post_frame; job.buffer = buffer; job.result = post_result_addr;
                const float conf = det_cfg.CONF, iou = det_cfg.IOU_THRESHOLD;
                std::memcpy(&job.confidence_bits, &conf, 4); std::memcpy(&job.iou_bits, &iou, 4);
                uint64_t lo = ~uint64_t(0), hi = 0;
                for (size_t i=0; i<6; ++i) {
                    const auto& t = outputs[i]; const auto shape = t.dtype()->shape;
                    if (shape.size()!=4 || !std::equal(shape.begin(),shape.end(),shapes[i]) ||
                        !t.dtype()->element_dtype.getStorageType().isFP32() || t.isOn(icraft::xrt::HostDevice::MemRegion()))
                        throw std::runtime_error("FPGA POST native tensor contract mismatch at " + std::to_string(i));
                    const uint64_t a = t.data().addr();
                    if (a + bytes[i] > 0x100000000ULL) throw std::runtime_error("FPGA POST tensor address overflow");
                    job.tensor[i] = {uint32_t(a),bytes[i],i&1?16U:4U,4};
                    lo=std::min(lo,a&~uint64_t(63)); hi=std::max(hi,(a+bytes[i]+63)&~uint64_t(63));
                }
                if (hi<=lo || hi-lo>0xffffffffULL) throw std::runtime_error("FPGA POST invalid arena");
                job.arena=uint32_t(lo); job.arena_bytes=uint32_t(hi-lo);
                post_controller.submit(job); post_controller.wait(std::chrono::milliseconds(4000));
                observation.wait_ms=xunjing::elapsedMs(begin,std::chrono::steady_clock::now());
                std::array<char,448> raw{};
                post_result_mem.read(raw.data(),post_result_addr-post_result_mem->begin.addr(),raw.size());
                std::memcpy(&observation.result,raw.data(),sizeof(observation.result));
                const auto& result=observation.result;
                if (result.count>32 || result.frame_id!=job.frame)
                    throw std::runtime_error("FPGA POST invalid count or stale frame id");
                for (size_t i=0;i<result.count;++i) {
                    if(result.class_id[i]>=det_cfg.N_CLASS || result.box[i][2]<result.box[i][0] ||
                       result.box[i][3]<result.box[i][1] || result.box[i][2]>FRAME_W || result.box[i][3]>FRAME_H)
                        throw std::runtime_error("FPGA POST invalid box/class");
                }
                observation.status=post_controller.status(); observation.success=true; stage.Success();
            } catch (const std::exception& e) {
                // A still-running reader must not outlive its tensor ownership.
                if (!POST_CPU_FALLBACK || (post_controller.status() & xunjing::postv2::kBusy)) throw;
                spdlog::error("[FPGA POST V2] {}; CPU fallback snapshot",e.what());
                // Snapshot while this session still owns the NPU/MMU mapping.
                for(auto& t:outputs) t=t.to(icraft::xrt::HostDevice::MemRegion());
            }
            const double total_ms=xunjing::elapsedMs(begin,std::chrono::steady_clock::now());
            post_timing << fpga_post_frame << ',' << observation.result.count << ',' << observation.status << ','
                        << observation.wait_ms << ',' << total_ms << ',' << (observation.success?"fpga_v2":"cpu_fallback") << ',' << source << ',' << buffer << ',' << timestamp << ','
                        << std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() << '\n';
            if(fpga_post_frame%30==0) {
                post_timing.flush();
                spdlog::info("[FPGA POST V2] frame={} count={} status=0x{:x} wait_ms={:.3f} backend={}",
                    fpga_post_frame,observation.result.count,observation.status,observation.wait_ms,
                    observation.success?"fpga_v2":"cpu_fallback");
            }
            std::lock_guard<std::mutex> lock(post_observations_mutex);
            post_observations.emplace(PostKey{source,buffer,timestamp},observation);
        };
    }

    // 创建用于yolo26的后处理函数“适配器”lambda， 签名完美匹配 PostProcessingFunc
    HDMIDisplayActor<FPAIDevice, IcoreMessageForPost>::PostProcessingFunc yolo26_post_processor =
        // 捕获所有“实现”需要的额外变量
        // 注意：这里的参数列表必须与 PostProcessingFunc 定义完全匹配
        // 使用[&]可以简化捕获列表
        [&](const IcoreMessageForPost &post_msg, cv::Mat &cvmat_to_draw)
    {
        if(xunjing::NpuFault().load()) return;
        auto ts_start = std::chrono::steady_clock::now();
        const double cpu_start = xunjing::threadCpuMs();
        if (post_msg.icore_tensors.empty())
        {
            LOG_WARN("[PostProcessor]", "Icore tensors are empty, cannot perform post-processing.");
            return;
        }
        // 从消息中获取source_id，这是查找正确上下文的关键
        int source_id = post_msg.meta.source_id;

        // 检查source_id的有效性，防止越界
        if (source_id < 0 || source_id >= CAMERA_COUNT)
        {
            LOG_ERROR("[PostProcessor]", "Invalid source_id {} received.", source_id);
            return;
        }
        xunjing::reference::FrameCapture reference_capture(cvmat_to_draw, source_id, std::to_string(post_msg.meta.timestamp));
        // 调用真正的实现，并传入所有捕获的参数
        auto &netinfo = netinfos[source_id];
        auto &last_results = last_results_group[source_id];
        auto &fps_calc = fps_calculators[source_id];

        fps_calc.tick();
        LOG_DEBUG("[AI POST]", "[{}] Before post_detpost_plin, last_results size: {}", source_id, std::get<0>(last_results).size());
        YoloPostResult post_results;
        auto ts_before_post = std::chrono::steady_clock::now();
        auto ts_before_duration = std::chrono::duration_cast<std::chrono::microseconds>(ts_before_post - ts_start);
        double fpga_post_wait_ms = 0.0;
        auto decode_yolo26_cpu = [&]() -> YoloPostResult {
            if (post_msg.icore_tensors.size() != 6)
                throw std::runtime_error("YOLO26 Conv model expects six output tensors");
            std::unique_lock<std::recursive_mutex> output_guard(xunjing::NpuExecutionMutex(), std::defer_lock);
            const bool device_outputs = std::any_of(post_msg.icore_tensors.begin(),post_msg.icore_tensors.end(),[](const auto& t){return !t.isOn(icraft::xrt::HostDevice::MemRegion());});
            if(xunjing::DualNpuEnabled() && device_outputs) {
                output_guard.lock();
                if(xunjing::NpuFault().load()) throw std::runtime_error("NPU fault latched");
                auto backend=icore_actor.getIcoreSessionGroups().at(source_id).at(post_msg.meta.buffer_index)->backends[0].cast<FPAIBackend>();
                if(backend->map_table.defined()) { fpai_device.updateMMU(backend->map_table); fpai_device.clearMMUCache(); }
            }
            std::vector<std::vector<float>> output_values(6);
            std::vector<xunjing::detection::Yolo26OutputView> output_views;
            output_views.reserve(6);
            for (std::size_t output_index = 0; output_index < 6; ++output_index)
            {
                const auto &output = post_msg.icore_tensors[output_index];
                reference_capture.DeviceOutput(output_index, output, output.isOn(icraft::xrt::HostDevice::MemRegion()));
                auto host_tensor = output.to(icraft::xrt::HostDevice::MemRegion());
                const auto shape_source = output.dtype()->shape;
                std::vector<int64_t> output_shape(shape_source.begin(), shape_source.end());
                std::size_t element_count = 1;
                for (const auto dimension : output_shape)
                    element_count *= static_cast<std::size_t>(dimension);
                output_values[output_index].resize(element_count);
                const auto storage_type = output.dtype()->element_dtype.getStorageType();
                if (storage_type.isFP32())
                {
                    const auto *source = reinterpret_cast<const float *>(host_tensor.data().cptr());
                    std::copy(source, source + element_count, output_values[output_index].begin());
                }
                else if (storage_type.isBF16())
                {
                    const auto *source = reinterpret_cast<const uint16_t *>(host_tensor.data().cptr());
                    for (std::size_t i = 0; i < element_count; ++i)
                    {
                        const uint32_t bits = static_cast<uint32_t>(source[i]) << 16;
                        std::memcpy(&output_values[output_index][i], &bits, sizeof(float));
                    }
                }
                else if (storage_type.isSInt16())
                {
                    const auto *source = reinterpret_cast<const int16_t *>(host_tensor.data().cptr());
                    std::transform(source, source + element_count, output_values[output_index].begin(),
                                   [&](int16_t value) { return value * YOLO26_OUTPUT_SCALE; });
                }
                else if (storage_type.isSInt8())
                {
                    const auto *source = reinterpret_cast<const int8_t *>(host_tensor.data().cptr());
                    std::transform(source, source + element_count, output_values[output_index].begin(),
                                   [&](int8_t value) { return value * YOLO26_OUTPUT_SCALE; });
                }
                else
                    throw std::runtime_error("Unsupported YOLO26 output dtype; export FP32/BF16 output or scaled signed integer");
                reference_capture.Output(output_index, output_values[output_index], output_shape);
                output_views.push_back({output_values[output_index].data(), output_shape});
            }
            if(output_guard.owns_lock()) output_guard.unlock();
            std::size_t candidates_before_nms = 0;
            const auto detections = xunjing::detection::DecodeYolo26Conv(
                output_views, det_cfg.N_CLASS, det_cfg.CONF, det_cfg.IOU_THRESHOLD,
                300, &candidates_before_nms);
            LOG_DEBUG("[YOLO26 CPU NMS]", "candidates={} kept={} iou={}",
                      candidates_before_nms, detections.size(), det_cfg.IOU_THRESHOLD);
            std::vector<int> ids;
            std::vector<float> scores;
            std::vector<cv::Rect2f> boxes;
            ids.reserve(detections.size());
            scores.reserve(detections.size());
            boxes.reserve(detections.size());
            for (const auto &detection : detections)
            {
                ids.push_back(detection.class_id);
                scores.push_back(detection.confidence);
                boxes.push_back(detection.box);
            }
            return {std::move(ids), std::move(scores), std::move(boxes)};
        };

        if (MODEL_FAMILY == "yolo26")
        {
            if (POST_BACKEND == "fpga_v2")
            {
                PostObservation observation;
                {
                    std::lock_guard<std::mutex> lock(post_observations_mutex);
                    const PostKey key{source_id,post_msg.meta.buffer_index,std::to_string(post_msg.meta.timestamp)};
                    auto found=post_observations.find(key);
                    if(found==post_observations.end()) throw std::runtime_error("Missing FPGA POST frame result");
                    observation=found->second; post_observations.erase(found);
                }
                fpga_post_wait_ms=observation.wait_ms;
                if(observation.success) {
                    const auto& result=observation.result;
                    std::vector<int> ids;
                    std::vector<float> scores;
                    std::vector<cv::Rect2f> boxes;
                    ids.reserve(result.count); scores.reserve(result.count); boxes.reserve(result.count);
                    const auto rb = camera_actor->getRatioBias();
                    const bool hw_resize = std::get<0>(rb);
                    const float ratio_w = std::get<1>(rb), ratio_h = std::get<2>(rb);
                    const float bias_w = static_cast<float>(std::get<3>(rb)), bias_h = static_cast<float>(std::get<4>(rb));
                    for (std::size_t i = 0; i < result.count; ++i)
                    {
                        const float original_x = static_cast<float>(result.box[i][0]);
                        const float original_y = static_cast<float>(result.box[i][1]);
                        const float original_w = static_cast<float>(result.box[i][2] - result.box[i][0]);
                        const float original_h = static_cast<float>(result.box[i][3] - result.box[i][1]);
                        const float network_x = hw_resize ? (original_x - bias_w) / ratio_w : original_x * ratio_w + bias_w;
                        const float network_y = hw_resize ? (original_y - bias_h) / ratio_h : original_y * ratio_h + bias_h;
                        const float network_w = hw_resize ? original_w / ratio_w : original_w * ratio_w;
                        const float network_h = hw_resize ? original_h / ratio_h : original_h * ratio_h;
                        ids.push_back(static_cast<int>(result.class_id[i]));
                        scores.push_back(static_cast<float>(result.score[i]) / 32768.0F);
                        boxes.emplace_back(network_x, network_y, network_w, network_h);
                    }
                    post_results = {std::move(ids), std::move(scores), std::move(boxes)};
                } else {
                    post_results=decode_yolo26_cpu();
                }
            }
            else
            {
                post_results = decode_yolo26_cpu();
            }
        }
        else if (netinfo.DetPost_on)
        {
            post_results = post_detpost_plin(
                post_msg.icore_tensors, last_results, netinfo,
                det_cfg.CONF, det_cfg.IOU_THRESHOLD, det_cfg.MULTILABEL, det_cfg.FPGA_NMS, det_cfg.N_CLASS, det_cfg.ANCHORS,
                device);
        }
        else
        {
            post_results = post_detpost_soft(
                post_msg.icore_tensors, last_results, det_cfg.LABELS, det_cfg.ANCHORS, netinfo,
                det_cfg.N_CLASS, det_cfg.CONF, det_cfg.IOU_THRESHOLD);
        }

        // Update last_results after the call. The SDK actor keeps its historical name.
        if (!SAVE)
            last_results = post_results;

        std::vector<int> id_list = std::get<0>(post_results);
        std::vector<float> score_list = std::get<1>(post_results);
        std::vector<cv::Rect2f> box_list = std::get<2>(post_results);
        LOG_DEBUG("[AI POST]", "[{}] After post_detpost_plin, last_results size: {}", source_id, std::get<0>(last_results).size());
        /*
         * user prompt: 定义一个lambda函数，将检测结果以yolo格式保存成txt，
         * 然后利用post_msg.meta.timestamp转成000000000139.jpg这样的名字，保存成txt。输出路径是./io/output/coco_val2017
         */
        if (SAVE)
        {
            auto save_yolo_result = [&](const std::vector<int> &ids, const std::vector<float> &scores, const std::vector<cv::Rect2f> &boxes)
            {
                std::string output_dir = "./io/output/coco_val2017";
                if (!fs::exists(output_dir))
                {
                    fs::create_directories(output_dir);
                }
                std::string filename;
                int org_width = CAMERA_W;
                int org_height = CAMERA_H;
                int net_width = det_cfg.NET_W;
                int net_height = det_cfg.NET_H;
                if (post_msg.image_name == "")
                {
                    filename = fmt::format("{}/{}.txt", output_dir, post_msg.meta.timestamp);
                }
                else
                {
                    org_width = post_msg.org_width;
                    org_height = post_msg.org_height;
                    net_width = post_msg.net_width;
                    net_height = post_msg.net_height;
                    filename = fmt::format("{}/{}.txt", output_dir, post_msg.image_name);
                }
                LOG_INFO("[TEST]", "Save YOLO result to {}", filename);
                std::ofstream outfile(filename, std::ios::out | std::ios::trunc);

                // 预处理是letterbox resize，所以这里用org_width和org_height还原
                if (outfile.is_open())
                {
                    float gain = std::min((float)net_width / org_width, (float)net_height / org_height);
                    int pad_w = (net_width - org_width * gain) / 2;
                    int pad_h = (net_height - org_height * gain) / 2;
                    std::cout << "Original image size: [" << org_width << "x" << org_height << "], Net input size: [" << net_width << "x" << net_height << "], gain: " << gain << ", pad_w: " << pad_w << ", pad_h: " << pad_h << std::endl;
                    for (size_t k = 0; k < boxes.size(); ++k)
                    {
                        // 还原到原始图像尺度
                        float left = (boxes[k].x - (float)pad_w) / gain;
                        float top = (boxes[k].y - (float)pad_h) / gain;
                        float width = boxes[k].width / gain;
                        float height = boxes[k].height / gain;
                        outfile << ids[k] << " " << left << " " << top << " " << width << " " << height << " " << scores[k] << "\n";
                    }
                    outfile.close();
                }
            };
            save_yolo_result(id_list, score_list, box_list);
        }
        auto ts_after_post = std::chrono::steady_clock::now();
        auto ts_after_duration = std::chrono::duration_cast<std::chrono::microseconds>(ts_after_post - ts_before_post);
        std::tuple<bool, float, float, int, int> ratio_bias = camera_actor->getRatioBias();
        bool is_hw_resize = std::get<0>(ratio_bias);
        float RATIO_W = std::get<1>(ratio_bias);
        float RATIO_H = std::get<2>(ratio_bias);
        int BIAS_W = std::get<3>(ratio_bias);
        int BIAS_H = std::get<4>(ratio_bias);

        std::vector<cv::Rect2f> display_boxes;
        display_boxes.reserve(box_list.size());
        for (const auto &box : box_list)
        {
            float x1, y1, w, h;
            if (is_hw_resize) // 先crop再resize
            {
                x1 = box.x * RATIO_W + BIAS_W;
                y1 = box.y * RATIO_H + BIAS_H;
                w = box.width * RATIO_W;
                h = box.height * RATIO_H;
            }
            else // 先resize再pad
            {
                x1 = (box.x - BIAS_W) / RATIO_W;
                y1 = (box.y - BIAS_H) / RATIO_H;
                w = box.width / RATIO_W;
                h = box.height / RATIO_H;
            }
            const float left = std::clamp(x1, 0.0F, static_cast<float>(FRAME_W - 1));
            const float top = std::clamp(y1, 0.0F, static_cast<float>(FRAME_H - 1));
            const float right = std::clamp(x1 + w, left, static_cast<float>(FRAME_W));
            const float bottom = std::clamp(y1 + h, top, static_cast<float>(FRAME_H));
            display_boxes.emplace_back(left, top, right - left, bottom - top);
        }

        reference_capture.Boxes(display_boxes, id_list, score_list);
        std::vector<xunjing::tracking::Detection> tracking_detections;
        if (TRACKER_ENABLED)
        {
            for (std::size_t index = 0; index < display_boxes.size(); ++index)
            {
                if (id_list[index] == TARGET_CLASS_ID &&
                    display_boxes[index].width >= 1.0F &&
                    display_boxes[index].height >= 1.0F)
                {
                    tracking_detections.push_back({display_boxes[index], score_list[index]});
                }
            }
        }

        const auto ts_tracker_start = std::chrono::steady_clock::now();
        xunjing::tracking::TrackerOutput tracker_output;
        if (TRACKER_ENABLED)
            tracker_output = target_tracker.Update(tracking_detections);
        reference_capture.Tracks(tracker_output.tracks);
        const auto ts_tracker_end = std::chrono::steady_clock::now();
        const auto tracker_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            ts_tracker_end - ts_tracker_start);

        std::optional<xunjing::classification::BehaviorResult> behavior_result;
        std::optional<float> distance_m;
        if (tracker_output.locked_target)
        {
            const auto &target = *tracker_output.locked_target;
            reference_capture.Target(target.box, target.id);
            distance_m = monocular_ranger.Estimate(target.id, target.box.height);
            behavior_result = behavior_classifier.Update(
                cvmat_to_draw, target.box, target.id);
        }
        else if (!target_tracker.LockedTrackId())
        {
            behavior_classifier.Reset();
            monocular_ranger.Reset();
        }

        const auto ts_classify_end = std::chrono::steady_clock::now();
        if (DRAW)
        {
            // Avoid duplicate detection/tracking boxes for the tracked class.
            for (std::size_t i=0; i<display_boxes.size(); ++i) {
                if (TRACKER_ENABLED && id_list[i]==TARGET_CLASS_ID) continue;
                cv::rectangle(cvmat_to_draw, display_boxes[i],
                    xunjing::overlayColor(cvmat_to_draw, classColor(id_list[i])), 2, cv::LINE_8);
            }
            track_overlay.Draw(cvmat_to_draw, tracker_output.tracks, [&](const auto& track) {
                auto text=fmt::format("{} ID:{} det:{:.0f}%",TARGET_CLASS,track.id,track.score*100.0F);
                if(track.locked && behavior_result)
                    text+=fmt::format(" {}:{:.0f}%",behavior_result->label,behavior_result->confidence*100.0F);
                if(track.locked && distance_m) text+=fmt::format(" {:.2f}m",*distance_m);
                reference_capture.Label(track.id, text);
                return text;
            });
            const auto global_label = fmt::format("FPS: {:.1f} Track: {:.2f}ms",fps_calc.getFPS(),tracker_duration.count()/1000.0F);
            reference_capture.GlobalLabel(global_label);
            xunjing::overlayLabel(cvmat_to_draw, global_label, {12,30},{255,255,255});
        }
        reference_capture.Commit(cvmat_to_draw);
        const auto ts_overlay_end = std::chrono::steady_clock::now();
        bool uart_sent = false;

        if (tracker_output.locked_target)
        {
            const auto &center = tracker_output.locked_target->center;
            const auto center_x = static_cast<std::uint16_t>(std::clamp(
                std::lround(center.x), 0L, static_cast<long>(FRAME_W - 1)));
            const auto center_y = static_cast<std::uint16_t>(std::clamp(
                std::lround(center.y), 0L, static_cast<long>(FRAME_H - 1)));
            task_publisher.Publish(center_x, center_y,
                                   distance_m.value_or(0.0F), &uart_sent,
                                   behavior_result ? task_uart::BehaviorFromLabel(behavior_result->label)
                                                   : task_uart::BehaviorState::Unknown);
        }
        auto ts_draw_end = std::chrono::steady_clock::now();
        auto ts_draw_duration = std::chrono::duration_cast<std::chrono::microseconds>(ts_draw_end - ts_after_post);
        auto ts_total = std::chrono::duration_cast<std::chrono::microseconds>(ts_draw_end - ts_start);
        timing_csv.Write(source_id, post_msg.meta.timestamp, box_list.size(), tracker_output.tracks.size(), uart_sent,
            xunjing::elapsedMs(ts_start,ts_after_post), xunjing::elapsedMs(ts_after_post,ts_tracker_start),
            xunjing::elapsedMs(ts_tracker_start,ts_tracker_end), xunjing::elapsedMs(ts_tracker_end,ts_classify_end),
            xunjing::elapsedMs(ts_classify_end,ts_overlay_end), xunjing::elapsedMs(ts_overlay_end,ts_draw_end),
            xunjing::elapsedMs(ts_start,ts_draw_end), xunjing::threadCpuMs()-cpu_start,
            tracker_output.locked_target ? tracker_output.locked_target->id : -1,
            tracker_output.locked_target ? tracker_output.locked_target->center.x : -1,
            tracker_output.locked_target ? tracker_output.locked_target->center.y : -1,
            tracker_output.locked_target ? tracker_output.locked_target->box.height : -1.0F,
            distance_m.value_or(-1.0F), behavior_result ? behavior_result->class_id : -1,
            behavior_result ? behavior_result->confidence : 0.0F,
            behavior_result && behavior_result->fresh, fpga_post_wait_ms);
        const std::array<double, 8> stage_ms{
            xunjing::elapsedMs(ts_start,ts_after_post),
            xunjing::elapsedMs(ts_after_post,ts_tracker_start),
            xunjing::elapsedMs(ts_tracker_start,ts_tracker_end),
            xunjing::elapsedMs(ts_tracker_end,ts_classify_end),
            xunjing::elapsedMs(ts_classify_end,ts_overlay_end),
            xunjing::elapsedMs(ts_overlay_end,ts_draw_end),
            xunjing::elapsedMs(ts_start,ts_draw_end), xunjing::threadCpuMs()-cpu_start};
        for (std::size_t stage = 0; stage < stage_ms.size(); ++stage)
            console_stage_ms[stage] += stage_ms[stage];
        console_fpga_post_ms += fpga_post_wait_ms;
        if (++console_frames % 30 == 0) {
            const auto now = std::chrono::steady_clock::now();
            const double fps = 30000.0 / xunjing::elapsedMs(console_window_start, now);
            spdlog::info("[PERF] frame={} window=30 fps={:.2f} post_backend={} fpga_wait_ms={:.3f} mean_ms: result={:.3f} map={:.3f} track={:.3f} classify_range={:.3f} draw={:.3f} uart={:.3f} post={:.3f} post_cpu={:.3f}",
                console_frames, fps, POST_BACKEND, console_fpga_post_ms/30, console_stage_ms[0]/30, console_stage_ms[1]/30,
                console_stage_ms[2]/30, console_stage_ms[3]/30, console_stage_ms[4]/30,
                console_stage_ms[5]/30, console_stage_ms[6]/30, console_stage_ms[7]/30);
            console_stage_ms.fill(0);
            console_fpga_post_ms = 0.0;
            console_window_start = now;
        }
        LOG_DEBUG("[AI POST]", "[{}] Post-processing time: total={}ms (before_post={}ms, after_post={}ms, draw={}ms)",
                  source_id,
                  ts_total.count() / 1000.0f,
                  ts_before_duration.count() / 1000.0f,
                  ts_after_duration.count() / 1000.0f,
                  ts_draw_duration.count() / 1000.0f);
    };

    // 配置HDMI输出
    int hdmi_id = 0;
    auto hdmi_display = std::make_unique<RGB565HDMIDisplay<FPAIDevice>>(hdmi_id, fpai_device, FRAME_W, FRAME_H);

    HDMIDisplayActor<FPAIDevice, IcoreMessageForPost> display_actor(hdmi_display->getSinkId(),
                                                                    std::move(hdmi_display),
                                                                    fpai_device,
                                                                    buffer_manager,
                                                                    camera_actor->getChunkGroupId(),
                                                                    yolo26_post_processor);
    display_actor.bindInputQueue(&icore_output_queue);

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    // 9. 启动所有Actors

    camera_actor->start();
    display_actor.start();
    icore_actor.start();

    spdlog::info("All actors started...");

    // Blocking loop that waits for the stop signal
    while (!stop_requested && !xunjing::NpuFault().load()) {
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int status = ::poll(&input, 1, 200);
        if (status > 0 && (input.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) break;
    } // Enter, Ctrl+C and SIGTERM all reach actor shutdown and CSV destructors.

    behavior_classifier.Stop();
    camera_actor->stop();
    icore_actor.stop();
    display_actor.stop();

    spdlog::info("All actors stopped...");
    Device::Close(device);

    return xunjing::NpuFault().load() ? 2 : 0;
}
