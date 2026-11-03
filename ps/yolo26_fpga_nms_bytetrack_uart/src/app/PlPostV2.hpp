#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <vector>

namespace xunjing::postv2 {
using U32 = std::uint32_t;
constexpr U32 kBase=0x400c3000, kId=0x584a5002, kLayoutReg=0xec, kLayout=0x00010020;
constexpr U32 kBusy=1, kDone=2, kError=4, kRejected=8, kReady=16;
struct Tensor { U32 base=0, bytes=0, cell_stride=0, channel_stride=0; };
struct Job { U32 confidence_bits=0x3f000000, iou_bits=0x3f19999a; U32 frame=0, buffer=0, arena=0, arena_bytes=0, result=0, result_capacity=448, timeout=100000000; std::array<Tensor,6> tensor{}; };
#pragma pack(push,1)
struct Result { U32 frame_id; std::uint16_t count,reserved; std::uint16_t box[32][4]; std::uint16_t score[32]; std::uint16_t class_id[32]; };
#pragma pack(pop)
static_assert(sizeof(Result)==392,"POST V2 result ABI");

template<class Regs> class Controller {
  Regs& r_;
  U32 rd(U32 o) const { return r_.read(kBase+o); }
  void wr(U32 o,U32 v) { r_.write(kBase+o,v); }
public:
  explicit Controller(Regs& r):r_(r){}
  U32 id() const { return rd(0); }
  U32 status() const { return rd(4); }
  U32 layout() const { return rd(kLayoutReg); }
  void requireReady() const {
    if(id()!=kId) throw std::runtime_error("POST V2 ID mismatch");
    if(layout()!=kLayout) throw std::runtime_error("POST V2 layout mismatch");
    auto s=status(); if(!(s&kReady) || (s&(kBusy|kError|kRejected))) throw std::runtime_error("POST V2 not READY");
  }
  void submit(const Job& j) {
    requireReady();
    if(!j.result || (j.result&63) || j.result_capacity<448 || (std::uint64_t(j.result)+j.result_capacity)>0x40000000ULL)
      throw std::invalid_argument("result buffer must be >=448 bytes, 64 aligned, below 0x40000000");
    if(!j.arena || (j.arena&63) || (j.arena_bytes&63)) throw std::invalid_argument("native arena alignment");
    wr(8,2); // CLEAR
    wr(0x0c,j.frame); wr(0x10,j.buffer); wr(0x14,32); wr(0x18,j.iou_bits);
    wr(0x1c,j.arena); wr(0x20,j.arena_bytes); wr(0x24,1); wr(0x28,1); wr(0x2c,0x584a0002);
    wr(0x4c,j.confidence_bits);
    wr(0xe0,j.result); wr(0xe4,j.result_capacity); wr(0xe8,j.timeout);
    for(unsigned i=0;i<6;++i){ auto&t=j.tensor[i]; wr(0x80+16*i,t.base); wr(0x84+16*i,t.bytes); wr(0x88+16*i,t.cell_stride); wr(0x8c+16*i,t.channel_stride); }
    wr(8,1); // START
  }
  void wait(std::chrono::milliseconds timeout) {
    auto end=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<end){ auto s=status(); if(s&kError) throw std::runtime_error("POST V2 ERROR"); if(s&kRejected) throw std::runtime_error("POST V2 REJECTED"); if((s&kDone)&&!(s&kBusy)) return; std::this_thread::sleep_for(std::chrono::microseconds(100)); }
    throw std::runtime_error("POST V2 timeout");
  }
};
}
