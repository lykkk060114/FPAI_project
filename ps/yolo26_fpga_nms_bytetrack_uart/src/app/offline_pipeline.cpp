// Replay the same C++ detection postprocessor, tracker, classifier and UART publisher
// against a video using ONNX Runtime instead of the board's detector backend.
#include "classification/BehaviorClassifier.hpp"
#include "detection/Yolo26Postprocess.hpp"
#include "tracking/TargetTracker.hpp"
#include "TaskPublisher.hpp"
#include "TrackOverlay.hpp"
#include "FrameTimingCsv.hpp"
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::cerr << "Usage: offline_pipeline detector.onnx classifier.onnx video report.json [uart-device|-] [max-frames=60]\n";
        return 2;
    }
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "offline_detector");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(4);
        Ort::Session detector(env, argv[1], options);
        Ort::AllocatorWithDefaultOptions allocator;
        const auto input_name = detector.GetInputNameAllocated(0, allocator);
        const auto output_name = detector.GetOutputNameAllocated(0, allocator);
        auto type = detector.GetInputTypeInfo(0);
        const auto shape = type.GetTensorTypeAndShapeInfo().GetShape();
        if (shape != std::vector<int64_t>{1, 3, 352, 640})
            throw std::runtime_error("detector input must be [1,3,352,640]");
        xunjing::classification::ClassifierConfig config;
        config.enabled = true;
        config.model_path = argv[2];
        config.labels = {"drink", "eat", "rest", "walk"};
        xunjing::classification::BehaviorClassifier classifier(config);
        if (!classifier.Ready()) throw std::runtime_error(classifier.LastError());
        xunjing::tracking::TargetTracker tracker({30, 15, 1920, 1080});
        xunjing::UartPublisherConfig uart;
        uart.enabled = argc > 5 && std::string(argv[5]) != "-";
        if (uart.enabled) uart.device = argv[5];
        xunjing::TaskPublisher publisher(uart);
        cv::VideoCapture capture(argv[3]);
        if (!capture.isOpened()) throw std::runtime_error("cannot open video");
        std::ofstream report(argv[4]);
        if (!report) throw std::runtime_error("cannot open output report");
        const int limit = argc > 6 ? std::stoi(argv[6]) : 60;
        int frames = 0, detected = 0, tracked = 0, classified = 0, fresh = 0;
        int publish_calls = 0, publish_failures = 0;
        report << "{\"backend\":\"host ONNX Runtime; production C++ postprocess/tracker/classifier/UART\",\"frames\":[";
        xunjing::TrackOverlay overlay;
        xunjing::FrameTimingCsv timing("offline_cpu", "host_detect_and_map_wall_ms,unused_map_ms,track_wall_ms,classify_uart_wall_ms,overlay_and_preview_wall_ms,unused_uart_ms,host_frame_wall_ms,host_thread_cpu_ms");
        cv::Mat source;
        while (frames < limit && capture.read(source)) {
            const auto begin=std::chrono::steady_clock::now();
            const auto cpu_begin=xunjing::threadCpuMs();
            cv::Mat frame;
            cv::resize(source, frame, {1920,1080});
            const float scale = std::min(640.0F / frame.cols, 352.0F / frame.rows);
            const int w = std::lround(frame.cols * scale), h = std::lround(frame.rows * scale);
            const int left = (640-w)/2, top = (352-h)/2;
            cv::Mat rgb, resized, padded;
            cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
            cv::resize(rgb, resized, {w,h});
            cv::copyMakeBorder(resized, padded, top, 352-h-top, left, 640-w-left,
                               cv::BORDER_CONSTANT, cv::Scalar(114,114,114));
            std::vector<float> input(3*352*640);
            for (int y=0; y<352; ++y) for (int x=0; x<640; ++x)
                for (int ch=0; ch<3; ++ch)
                    input[ch*352*640+y*640+x] = padded.at<cv::Vec3b>(y,x)[ch]/255.0F;
            const std::array<int64_t,4> input_shape{1,3,352,640};
            const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), input_shape.data(), 4);
            const char* inputs[]{input_name.get()}; const char* outputs[]{output_name.get()};
            auto result = detector.Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
            const auto output_shape = result[0].GetTensorTypeAndShapeInfo().GetShape();
            const auto detections = xunjing::detection::DecodeYolo26(result[0].GetTensorData<float>(), output_shape, 1, .5F, .6F);
            std::vector<xunjing::tracking::Detection> objects;
            for (const auto& d : detections) {
                const cv::Rect2f box{(d.box.x-left)/scale, (d.box.y-top)/scale,
                                     d.box.width/scale, d.box.height/scale};
                objects.push_back({box,d.confidence});
            }
            if (!objects.empty()) ++detected;
            const auto decode_end=std::chrono::steady_clock::now();
            const auto output = tracker.Update(objects);
            const auto track_end=std::chrono::steady_clock::now();
            bool sent=false;
            cv::Mat rgb565;
            cv::cvtColor(frame, rgb565, cv::COLOR_BGR2BGR565);
            if (frames) report << ',';
            report << "{\"frame\":" << frames << ",\"detections\":" << detections.size();
            if (output.locked_target) {
                ++tracked;
                const auto& target = *output.locked_target;

                const auto behavior = classifier.Update(rgb565, target.box, target.id);
                if (!behavior) throw std::runtime_error("classification failed: " + classifier.LastError());
                ++classified; fresh += behavior->fresh;
                report << ",\"track_id\":" << target.id << ",\"behavior\":\"" << behavior->label
                       << "\",\"confidence\":" << behavior->confidence;
                if (uart.enabled) {
                    ++publish_calls;
                    // Range is uncalibrated: preserve the production protocol's zero sentinel.
                    const bool publish_ok=publisher.Publish(std::clamp(static_cast<int>(target.center.x),0,1919),
                                           std::clamp(static_cast<int>(target.center.y),0,1079),0.0F, &sent, task_uart::BehaviorFromLabel(behavior->label));
                    if(!publish_ok) ++publish_failures;
                }
            } else if (!tracker.LockedTrackId()) classifier.Reset();
            const auto tasks_end=std::chrono::steady_clock::now();
            overlay.Draw(rgb565,output.tracks,[](const auto& t){return "panda ID:"+std::to_string(t.id);});
            if(frames==limit-1) {
                cv::Mat preview; cv::cvtColor(rgb565,preview,cv::COLOR_BGR5652BGR);
                cv::imwrite(std::string(argv[4])+".overlay.png",preview);
            }
            const auto end=std::chrono::steady_clock::now();
            timing.Write(0,frames,detections.size(),output.tracks.size(),sent,
                xunjing::elapsedMs(begin,decode_end),0,xunjing::elapsedMs(decode_end,track_end),
                xunjing::elapsedMs(track_end,tasks_end),xunjing::elapsedMs(tasks_end,end),0,
                xunjing::elapsedMs(begin,end),xunjing::threadCpuMs()-cpu_begin);
            report << '}'; ++frames;
        }
        report << "],\"summary\":{\"frames\":" << frames << ",\"detected_frames\":" << detected
               << ",\"tracked_frames\":" << tracked << ",\"classified_frames\":" << classified
               << ",\"fresh_classifications\":" << fresh << ",\"uart_publish_calls\":" << publish_calls
               << ",\"uart_publish_failures\":" << publish_failures << "}}\n";
        std::cout << "frames=" << frames << " tracked=" << tracked << " classified=" << classified
                  << " uart_calls=" << publish_calls << " uart_failures=" << publish_failures << '\n';
        return frames && tracked && classified && !publish_failures ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
