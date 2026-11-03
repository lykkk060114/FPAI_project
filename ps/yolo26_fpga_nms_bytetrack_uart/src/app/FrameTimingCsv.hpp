#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iostream>
namespace xunjing {
inline double threadCpuMs() { timespec t{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }
inline double elapsedMs(std::chrono::steady_clock::time_point a,std::chrono::steady_clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
class FrameTimingCsv {
    std::ofstream file_; size_t rows_=0;
public:
    FrameTimingCsv(const std::string& prefix="cpu_timing", const std::string& stages="decode_wall_ms,map_wall_ms,track_wall_ms,classify_range_wall_ms,draw_wall_ms,uart_wall_ms,post_wall_ms,post_thread_cpu_ms") {
        try {
            std::filesystem::create_directories("results");
            auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
            auto path="results/"+prefix+"_"+std::to_string(stamp)+".csv";
            file_.open(path); if(!file_) throw std::runtime_error("cannot open timing CSV");
            file_<<"wall_epoch_ns,steady_ns,source_id,source_timestamp,detections,tracks,uart_sent,"<<stages<<",track_id,center_x,center_y,bbox_height_px,distance_m,behavior_id,behavior_confidence,behavior_fresh,fpga_post_wait_ms\n";
            std::cerr<<"CPU timing: "<<path<<'\n';
        } catch(const std::exception& e) { std::cerr<<"Timing disabled: "<<e.what()<<'\n'; }
    }
    template<class Timestamp> void Write(int source,Timestamp stamp,size_t det,size_t tracks,bool sent,double decode,double map,double track,double classify,double draw,double uart,double total,double cpu,int track_id=-1,float x=-1,float y=-1,float bbox_height=-1,float distance=-1,int behavior=-1,float confidence=0,bool fresh=false,double fpga_post_wait_ms=0.0) {
        if(!file_) return;
        file_<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()<<','
             <<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()<<','
             <<source<<','<<stamp<<','<<det<<','<<tracks<<','<<sent<<','<<decode<<','<<map<<','<<track<<','<<classify<<','<<draw<<','<<uart<<','<<total<<','<<cpu<<','<<track_id<<','<<x<<','<<y<<','<<bbox_height<<','<<distance<<','<<behavior<<','<<confidence<<','<<fresh<<','<<fpga_post_wait_ms<<'\n';
        if(++rows_%30==0) file_.flush();
    }
};
}
