#pragma once
// Contract: peer POST header 20261023 + CLASS v2 table 20261025.
// Addresses below are User_Ddr addresses, never PS pointers or udmabuf addresses.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>
namespace xunjing::pl {
using U32=std::uint32_t;
constexpr U32 ClassBase=0x400c1000, PostBase=0x400c3000, InputBase=0x400c4000;
constexpr U32 Busy=1,Done=2,Error=4,Rejected=8,Ready=16;
struct Mmio { std::function<U32(U32)> read; std::function<void(U32,U32)> write; };
struct Range { U32 base, bytes; };
inline void CheckRange(Range r,U32 minimum,U32 alignment) {
    if(!r.base || !r.bytes || r.bytes<minimum || r.base%alignment ||
       std::uint64_t(r.base)+r.bytes>0x100000000ULL)
        throw std::invalid_argument("invalid User_Ddr range/capacity/alignment");
}
inline bool Overlap(Range a,Range b) {
    return std::uint64_t(a.base)<std::uint64_t(b.base)+b.bytes &&
           std::uint64_t(b.base)<std::uint64_t(a.base)+a.bytes;
}
struct ClassJob {
    U32 frame,buffer; Range source,destination; U32 stride,x,y,width,height;
};
struct TensorDesc { Range memory; U32 cell_stride,channel_stride; };
struct PostJob { U32 frame,buffer; Range arena; std::array<TensorDesc,6> tensors; };
struct Record { U32 class_id,index; float score,x,y,width,height; };
class Page {
    Mmio io_; U32 base_,id_; bool owned_=false,failed_=false;
public:
    Page(Mmio io,U32 base,U32 id):io_(std::move(io)),base_(base),id_(id){}
    U32 read(U32 offset)const{return io_.read(base_+offset);}
    void write(U32 offset,U32 word){io_.write(base_+offset,word);}
    bool ownsBuffers()const{return owned_;}
    bool failed()const{return failed_;}
    void checkReady()const {
        if(owned_ || failed_)throw std::runtime_error("PL buffers retained; recovery/reset required");
        if(read(0)!=id_)throw std::runtime_error("PL interface ID mismatch");
        const auto s=read(4);
        if(!(s&Ready)||(s&(Busy|Error|Rejected|Done)))throw std::runtime_error("PL page not clean READY; explicit clear required");
    }
    void clear() {
        if(owned_ || failed_)throw std::runtime_error("cannot CLEAR an owned/faulted job");
        if(read(0)!=id_ || (read(4)&Busy))throw std::runtime_error("cannot CLEAR busy/wrong PL page");
        write(8,2);
    }
    void start(){owned_=true;try{write(8,1);}catch(...){failed_=true;throw;}}
    // No automatic CLEAR/ABORT/recycle on timeout or error. Caller retains all
    // source/destination tensors and the active MMU map until proven quiescent.
    void wait(std::chrono::milliseconds timeout) {
        if(!owned_ || failed_)throw std::runtime_error("no healthy PL job to wait for");
        const auto end=std::chrono::steady_clock::now()+timeout;
        try {
            do {
                const auto s=read(4);
                if(s&(Error|Rejected))throw std::runtime_error("PL job error/rejected");
                if((s&Done)&&!(s&Busy))return;
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }while(std::chrono::steady_clock::now()<end);
            throw std::runtime_error("PL job timeout; buffers remain owned");
        }catch(...){failed_=true;throw;}
    }
    void releaseAfterVisibility(const std::function<bool()>& fence) {
        if(!owned_ || failed_)throw std::runtime_error("PL job not releasable");
        const auto s=read(4);
        if((s&(Done|Busy|Error|Rejected))!=Done)throw std::runtime_error("PL job not complete");
        // Not replaced by sleep, dmb, MMU invalidation or written_beats.
        if(!fence || !fence())throw std::runtime_error("platform device visibility not verified");
        owned_=false;
    }
};
class ClassV2:public Page {
public:
    explicit ClassV2(Mmio io):Page(std::move(io),ClassBase,0x584a4302){}
    void submit(const ClassJob& j) {
        CheckRange(j.source,1,8);CheckRange(j.destination,602112,64);
        if(j.buffer>3 || j.stride<3840 || j.stride%2 ||
           std::uint64_t(j.stride)*1079+3840>j.source.bytes ||
           !j.width || !j.height || std::uint64_t(j.x)+j.width>1920 ||
           std::uint64_t(j.y)+j.height>1080 || Overlap(j.source,j.destination))
            throw std::invalid_argument("CLASS source/ROI/buffer mismatch");
        checkReady();if(read(0x48)!=3)throw std::runtime_error("CLASS RGB FP32 NHWC capability mismatch");
        const std::array<U32,13> fields={j.frame,j.source.base,j.stride,j.source.bytes,
            j.x,j.y,j.width,j.height,0,0,j.destination.base,j.destination.bytes,j.buffer};
        for(unsigned i=0;i<fields.size();++i)write(0xc+4*i,fields[i]);
        start();
    }
    void finish(const std::function<bool()>& visibility) {
        if(read(0x40)!=9408)throw std::runtime_error("CLASS incomplete write handshake count");
        releaseAfterVisibility(visibility);
    }
};
class Post:public Page {
public:
    explicit Post(Mmio io):Page(std::move(io),PostBase,0x584a5001){}
    void submit(const PostJob& j) {
        // The arena bounds READ access to the six tensors; results live in CSR RAM.
        CheckRange(j.arena,64,64);
        if(j.arena.bytes%64)throw std::invalid_argument("POST arena length must cover whole 64-byte lines");
        if(j.buffer>3)throw std::invalid_argument("POST buffer outside 0..3");
        // Current single-class YOLO26: cls,box at stride8,16,32; FP32.
        const U32 cells[6]={3520,3520,880,880,220,220};
        for(unsigned i=0;i<6;++i){const auto& t=j.tensors[i];
            CheckRange(t.memory,4,4);const U32 channels=(i%2)?4:1;
            if(t.cell_stride<4 || t.cell_stride%4 || (channels>1 && (t.channel_stride<4 || t.channel_stride%4)) ||
               std::uint64_t(cells[i]-1)*t.cell_stride+std::uint64_t(channels-1)*t.channel_stride+4>t.memory.bytes ||
               t.memory.base<j.arena.base ||
               ((std::uint64_t(t.memory.base)+(cells[i]-1)*std::uint64_t(t.cell_stride)+(channels-1)*std::uint64_t(t.channel_stride)+3)|63ULL)>=std::uint64_t(j.arena.base)+j.arena.bytes)
                throw std::invalid_argument("POST descriptor layout/read arena mismatch");
        }
        checkReady();
        const std::array<U32,9> f={j.frame,j.buffer,300,0x3f19999a,j.arena.base,j.arena.bytes,1,1,0x584a0001};
        for(unsigned i=0;i<f.size();++i)write(0xc+4*i,f[i]);
        for(unsigned i=0;i<6;++i){const auto& t=j.tensors[i];write(0x80+16*i,t.memory.base);write(0x84+16*i,t.memory.bytes);write(0x88+16*i,t.cell_stride);write(0x8c+16*i,t.channel_stride);}
        start();
    }
    std::vector<Record> records(std::chrono::milliseconds timeout) {
        const auto status=read(4);
        if(!ownsBuffers() || failed() || (status&(Done|Busy|Error|Rejected))!=Done)
            throw std::runtime_error("POST result not ready");
        const auto n=read(0x3c);if(n>300)throw std::runtime_error("POST result count exceeds 300");
        const auto end=std::chrono::steady_clock::now()+timeout;
        std::vector<Record> result;
        for(U32 i=0;i<n;++i){std::array<U32,7> words{};
            for(U32 f=0;f<7;++f){write(0x40,i|(f<<9));
                while(!(read(0x48)&1)){
                    if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("POST record port timeout; buffers retained");
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
                words[f]=read(0x44);
            }
            float values[5];std::memcpy(values,words.data()+2,sizeof(values));
            for(auto v:values)if(!std::isfinite(v))throw std::runtime_error("POST non-finite record");
            if(words[0]!=0 || words[1]>=4620 || values[0]<0.5f || values[0]>1 || values[3]<=0 || values[4]<=0)
                throw std::runtime_error("POST record contract mismatch");
            result.push_back({words[0],words[1],values[0],values[1],values[2],values[3],values[4]});
        }
        return result;
    }
};
} // namespace xunjing::pl
