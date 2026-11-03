#pragma once
// New BOOT INPUT bridge integration. Never writes pixels with Tensor.write().
// Startup/periodic SDK readback validates payloads. Pipelined mode is verified
// against a same-byte NPU reference on board, not a vendor-documented fence.
#include "FrameTimingCsv.hpp"
#include "NpuExecution.hpp"
#include <icraft-xrt/core/device.h>
#include <icraft-xrt/core/tensor.h>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace xunjing {
inline bool& PlDirectEnabled(){static bool enabled=false;return enabled;}
class PlInputBridge {
    icraft::xrt::Device device_;
    icraft::xrt::MemChunk legacy_scratch_;
    bool active_=false, poisoned_=false;
    uint64_t address_=0, frame_=0;
    uint32_t fifo_full_before_=0;
    std::chrono::steady_clock::time_point start_;
    std::ofstream timing_;
    std::vector<char> raw_;
    std::shared_lock<std::shared_mutex> transport_;
    static constexpr uint64_t Base=0x400c4000, Bytes=2703360;
    uint32_t Read(unsigned o){return device_.defaultRegRegion().read(Base+o);}
    void Write(unsigned o,uint32_t v){device_.defaultRegRegion().write(Base+o,v);}
    static icraft::xrt::MemChunk Scratch(icraft::xrt::Device device) {
        std::lock_guard<std::recursive_mutex> lock(NpuExecutionMutex());
        return device.defaultMemRegion().malloc(1024*1024,false);
    }
    void Abort(){
        Write(8,4);auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<until){auto s=Read(4);if(!(s&1)&&(s&2)){active_=false;if(transport_.owns_lock())transport_.unlock();return;}std::this_thread::sleep_for(std::chrono::microseconds(100));}
        poisoned_=true;NpuFault().store(true);throw std::runtime_error("PL INPUT ABORT timeout; hardware reset required");
    }
public:
    explicit PlInputBridge(icraft::xrt::Device device):device_(device),legacy_scratch_(Scratch(device)),raw_(Bytes){
        if(Read(0)!=0x584a4901)throw std::runtime_error("PL INPUT CSR ID mismatch");
        std::filesystem::create_directories("results");
        auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        timing_.open("results/pl_direct_"+std::to_string(stamp)+".csv");
        if(!timing_)throw std::runtime_error("Cannot open PL input timing");
        timing_<<"frame_id,buffer_index,input_addr,capture_wall_ms,read_validate_wall_ms,commit_wall_ms,total_wall_ms,vs_start,capture_end,last_de,fifo_full,payload_verified,read_bytes,sdk_read_wall_ms,scan_wall_ms\n";
    }
    uint64_t LegacyAddress() const{return legacy_scratch_->begin.addr();}
    void Begin(uint64_t address,unsigned buffer){
        if(poisoned_||NpuFault().load())throw std::runtime_error("PL INPUT fault latched");
        if(active_)throw std::runtime_error("PL INPUT already owns buffer");
        if(address>0xffffffffULL-Bytes||(address&63))throw std::runtime_error("NPU input address incompatible with native32 CSR");
        if(PlPipelineEnabled())transport_=std::shared_lock<std::shared_mutex>(InputTransportMutex());
        Write(8,2);auto s=Read(4);if(!(s&16)||(s&1))throw std::runtime_error("PL INPUT not READY");
        address_=address; ++frame_;fifo_full_before_=Read(0x3c);
        Write(0xc,address);Write(0x10,Bytes);Write(0x14,frame_);Write(0x18,buffer);Write(0x1c,100000000);Write(0x20,64);
        start_=std::chrono::steady_clock::now();Write(8,1);active_=true;
    }
    template<class Tensor> void Complete(Tensor& input,unsigned buffer){
        try{
            if(input.data().addr()!=address_||(input.dtype()->shape.size()!=4 || input.dtype()->shape[0]!=1 || input.dtype()->shape[1]!=352 || input.dtype()->shape[2]!=640 || input.dtype()->shape[3]!=3)||!input.dtype()->element_dtype.getStorageType().isFP32())throw std::runtime_error("PL INPUT tensor contract/address mismatch");
            auto deadline=start_+std::chrono::seconds(2);uint32_t status=0;
            do{status=Read(4);if(status&(4|8))break;if((status&32)&&Read(0x24)==42240&&Read(0x38)==225280)break;std::this_thread::sleep_for(std::chrono::microseconds(100));}while(std::chrono::steady_clock::now()<deadline);
            const auto beats=Read(0x24),pixels=Read(0x38),full=Read(0x3c);
            if(!(status&32)||(status&(4|8))||beats!=42240||pixels!=225280||full!=fifo_full_before_)throw std::runtime_error("PL INPUT incomplete frame, status="+std::to_string(status)+", beats="+std::to_string(beats)+", pixels="+std::to_string(pixels)+", fifo_full_delta="+std::to_string(uint32_t(full-fifo_full_before_)));
            auto captured=std::chrono::steady_clock::now();
            // ACCEPTED terminates this frame's native writer. Release before
            // acquiring SDK mutex, otherwise reset -> writer -> SDK deadlocks.
            if(transport_.owns_lock())transport_.unlock();
            const bool verify=!PlPipelineEnabled()||frame_<=4||(PlVerifyInterval()&&frame_%PlVerifyInterval()==0);
            auto read_end=captured,scan_end=captured;
            if(verify){
            std::lock_guard<std::recursive_mutex> sdk_access(NpuExecutionMutex());
            input.chunk().read(raw_.data(),input.offset(),Bytes);
            read_end=std::chrono::steady_clock::now();
            for(size_t i=0;i<Bytes;i+=4){float v;std::memcpy(&v,raw_.data()+i,4);if(!std::isfinite(v)||v<0||v>255||v!=std::floor(v))throw std::runtime_error("PL INPUT payload is not u8-valued FP32");}
            scan_end=std::chrono::steady_clock::now();
            if(frame_<=4){std::ofstream f("results/pl_direct_input_"+std::to_string(buffer)+".f32",std::ios::binary);f.write(raw_.data(),Bytes);if(!f)throw std::runtime_error("Cannot save direct input");}
            }
            auto checked=std::chrono::steady_clock::now();
            // All frames still require ACCEPTED, exact beat/pixel counts and
            // no FIFO error before COMMIT. Never replace completion with delay.
            Write(8,8);
            auto until=checked+std::chrono::seconds(2);
            do{status=Read(4);if((status&2)&&!(status&1))break;std::this_thread::sleep_for(std::chrono::microseconds(100));}while(std::chrono::steady_clock::now()<until);
            if((status&7)!=2)throw std::runtime_error("PL INPUT COMMIT failed");
            active_=false;auto end=std::chrono::steady_clock::now();
            timing_<<frame_<<','<<buffer<<','<<address_<<','<<elapsedMs(start_,captured)<<','<<elapsedMs(captured,checked)<<','<<elapsedMs(checked,end)<<','<<elapsedMs(start_,end)<<','<<Read(0x30)<<','<<Read(0x34)<<','<<Read(0x38)<<','<<Read(0x3c)<<','<<verify<<','<<(verify?Bytes:0)<<','<<elapsedMs(captured,read_end)<<','<<elapsedMs(read_end,scan_end)<<'\n';
            if(frame_%30==0)timing_.flush();
            if(frame_==1)std::cerr<<"PL DIRECT: FPGA -> actual NPU input; pipeline="<<PlPipelineEnabled()<<"; verification first4 then every "<<PlVerifyInterval()<<" frames; no CPU conversion/Tensor.write\n";
        }catch(...){if(active_)Abort();throw;}
    }
};
}
