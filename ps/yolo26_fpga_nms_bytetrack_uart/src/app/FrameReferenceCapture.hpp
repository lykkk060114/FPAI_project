#pragma once
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace xunjing::reference {
// Opt-in logical captures. Never interprets a Tensor pointer as a device address.
// Files are diagnostic I/O and must be disabled for performance measurements.
class FrameCapture {
    inline static thread_local FrameCapture* current_ = nullptr;
    inline static std::atomic<unsigned> sequence_{0};
    FrameCapture* previous_ = nullptr;
    std::filesystem::path dir_;
    cv::FileStorage meta_;
    bool active_=false, failed_=false, classified_=false;
    template<class F> void attempt(F f) noexcept {
        if(!active_ || failed_)return;
        try{f();}catch(const std::exception& e){failed_=true;std::cerr<<"reference capture failed: "<<e.what()<<'\n';}
    }
    void bytes(const std::string& name,const void* data,std::size_t size){
        std::ofstream f(dir_/name,std::ios::binary);f.write(static_cast<const char*>(data),size);
        if(!f)throw std::runtime_error("capture write failed");
    }
    void image(const std::string& name,const cv::Mat& m){
        std::ofstream f(dir_/(name+".rgb565le.bin"),std::ios::binary);
        for(int y=0;y<m.rows;y++)f.write(reinterpret_cast<const char*>(m.ptr(y)),m.cols*m.elemSize());
        if(!f)throw std::runtime_error("capture frame write failed");
        cv::Mat bgr;cv::cvtColor(m,bgr,cv::COLOR_BGR5652BGR);
        if(!cv::imwrite((dir_/(name+".png")).string(),bgr))throw std::runtime_error("capture PNG failed");
    }
public:
    FrameCapture(const cv::Mat& frame,int source,const std::string& timestamp):previous_(current_){
        static const std::string root=[](){const char* p=std::getenv("XUNJING_REFERENCE_DIR");return p?std::string(p):std::string();}();
        if(root.empty())return;
        const auto slot=sequence_.fetch_add(1);if(slot>=10)return;
        active_=true;
        attempt([&]{
            uint32_t endian=1;
            if(*reinterpret_cast<unsigned char*>(&endian)!=1 || frame.type()!=CV_8UC2)throw std::runtime_error("capture requires little endian RGB565");
            dir_=std::filesystem::path(root)/("frame_"+std::to_string(slot)+"_"+std::to_string(source)+"_"+timestamp);
            if(!std::filesystem::create_directories(dir_))throw std::runtime_error("capture directory already exists; use a new output root");
            meta_.open((dir_/"capture.pending.json").string(),cv::FileStorage::WRITE|cv::FileStorage::FORMAT_JSON);
            if(!meta_.isOpened())throw std::runtime_error("capture metadata open failed");
            meta_<<"provenance"<<"board_runtime_logical_capture"<<"source_id"<<source<<"source_timestamp"<<timestamp
                 <<"identity_semantics"<<"SDK message association; hardware same-frame identity not independently verified"
                 <<"source_format"<<"RGB565_LE"<<"width"<<frame.cols<<"height"<<frame.rows
                 <<"stored_row_stride"<<int(frame.cols*frame.elemSize())<<"opencv"<<CV_VERSION
                 <<"outputs_are_physical_ddr_dump"<<false;
            image("before_draw",frame);
        });
        if(!failed_)current_=this;
    }
    ~FrameCapture(){current_=previous_;}
    FrameCapture(const FrameCapture&)=delete;
    static void Classification(const cv::Rect& roi,const cv::Mat& rgb,const std::vector<float>& input) noexcept {
        auto* p=current_;if(!p)return;
        p->attempt([&]{
            p->classified_=true;p->meta_<<"classification_roi_xywh"<<roi
              <<"classification_shape"<<std::vector<int>{1,3,rgb.rows,rgb.cols};
            p->bytes("classifier_input.f32le.bin",input.data(),input.size()*sizeof(float));
            for(int y=0;y<rgb.rows;y++)if(!rgb.ptr(y))throw std::runtime_error("bad RGB row");
            cv::Mat continuous=rgb.isContinuous()?rgb:rgb.clone();
            p->bytes("classifier_input.rgb888.bin",continuous.data,continuous.total()*continuous.elemSize());
            cv::Mat bgr;cv::cvtColor(rgb,bgr,cv::COLOR_RGB2BGR);
            if(!cv::imwrite((p->dir_/"classifier_input.png").string(),bgr))throw std::runtime_error("classification PNG failed");
        });
    }
    // SDK memory descriptors only: these addresses still need AXI/MMU translation verification.
    template<class TensorT> void DeviceOutput(std::size_t i,const TensorT& tensor,bool host_region) noexcept {
        attempt([&]{
            const auto chunk=tensor.chunk();
            meta_<<("output_"+std::to_string(i)+"_sdk_memory")<<"{"
              <<"region_type"<<std::string(tensor.memRegion()->typeKey())
              <<"is_host_region"<<host_region
              <<"tensor_data_addr_decimal"<<std::to_string(tensor.data().addr())
              <<"chunk_begin_addr_decimal"<<std::to_string(chunk->begin.addr())
              <<"tensor_offset_bytes_decimal"<<std::to_string(tensor.offset())
              <<"chunk_allocated_bytes_decimal"<<std::to_string(chunk->byte_size)
              <<"dtype_reported_bytes_decimal"<<std::to_string(tensor.dtype().bytes())
              <<"is_ready"<<tensor.isReady()
              <<"address_semantics"<<"SDK region address; PL AXI/MMU reachability not verified"<<"}";
        });
    }
    void Output(std::size_t i,const std::vector<float>& data,const std::vector<int64_t>& shape) noexcept {
        attempt([&]{bytes("output_"+std::to_string(i)+".f32le.bin",data.data(),data.size()*4);
          std::vector<int> s(shape.begin(),shape.end());meta_<<("output_"+std::to_string(i)+"_shape")<<s;});
    }
    void Label(int id,const std::string& text) noexcept {attempt([&]{meta_<<("track_label_"+std::to_string(id))<<text;});}
    void GlobalLabel(const std::string& text) noexcept {attempt([&]{meta_<<"global_label"<<text;});}
    void Target(const cv::Rect2f& box,int id) noexcept {attempt([&]{meta_<<"locked_track_id"<<id<<"target_original_xywh"<<box;});}
    void Boxes(const std::vector<cv::Rect2f>& boxes,const std::vector<int>& ids,const std::vector<float>& scores) noexcept {
        attempt([&]{
            meta_<<"detections_original_space"<<"[";
            for(size_t i=0;i<boxes.size();i++) {
                meta_<<"{"<<"class_id"<<ids.at(i)<<"score"<<scores.at(i)<<"xywh"<<boxes[i]<<"}";
            }
            meta_<<"]";
        });
    }
    template<class RangeT> void Tracks(const RangeT& tracks) noexcept {
        attempt([&]{
            meta_<<"tracks_original_space"<<"[";
            for(const auto&t:tracks) {
                meta_<<"{"<<"id"<<t.id<<"xywh"<<t.box<<"score"<<t.score<<"locked"<<t.locked<<"}";
            }
            meta_<<"]";
        });
    }
    void Commit(const cv::Mat& frame) noexcept {attempt([&]{
        image("after_draw",frame);meta_<<"classification_tensor_present"<<classified_<<"complete"<<true;meta_.release();
        std::filesystem::rename(dir_/"capture.pending.json",dir_/"capture.json");
    });}
};
} // namespace xunjing::reference
