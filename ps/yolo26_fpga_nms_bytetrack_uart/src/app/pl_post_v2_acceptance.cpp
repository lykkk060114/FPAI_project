#include <icraft-utils/logging.h>
#include "icraft_utils.hpp"
#include "compile_fpai_target.hpp"
#include "detection/Yolo26Postprocess.hpp"
#include "PlPostV2.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <iomanip>
using namespace std;
static vector<char> load(const string&p){ifstream f(p,ios::binary|ios::ate); if(!f) throw runtime_error("cannot open "+p); auto n=(size_t)f.tellg(); f.seekg(0); vector<char>b(n); f.read(b.data(),n); return b;}
template<class T> static vector<char> dump(T&t){ if(!t.waitForReady(chrono::milliseconds(10000))) throw runtime_error("tensor timeout"); ostringstream s; t.dump(s,"SFB"); auto x=s.str(); return vector<char>(x.begin(),x.end()); }
int main(int argc,char**argv){ try {
  if(argc<2) throw runtime_error("usage: pl_post_v2_acceptance post <input.f32> [frames] | regs");
  auto dev=icraft::xrt::Device::Open("axi://zg330aiu?npu=0x40000000&dma=0x80000000"); auto regs=dev.defaultRegRegion(); xunjing::postv2::Controller<decltype(regs)> ctl(regs);
  if(string(argv[1])=="regs"){ cout<<hex<<"post_id=0x"<<ctl.id()<<" status=0x"<<ctl.status()<<" layout=0x"<<ctl.layout()<<dec<<"\n"; return ctl.id()==xunjing::postv2::kId?0:2; }
  if(argc<3) throw runtime_error("post requires input.f32");
  auto bytes=load(argv[2]); if(bytes.size()!=2703360) throw runtime_error("input must be 2703360 bytes FP32");
  auto net=loadNetwork("imodel/ZG/yolo26_panda_conv_ZG.json","imodel/ZG/yolo26_panda_conv_ZG.raw"); auto session=initSession("zg330",net.viewExcept({0,1}),dev,-1,true,false,false); session.apply();
  auto backend=session->backends[0].cast<icraft::xrt::zg330::ZG330Backend>(); auto op=get<0>(session.getForwards().at(0)); auto info=backend->forward_info->value_map.at(op->inputs[0]->v_id); auto chunk=backend->forward_info->memchunk_map.at(op->inputs[0]->v_id)->memChunk; icraft::xrt::Tensor input(info->value); input.setData(chunk,info->phy_addr-chunk->begin.addr()); input.write(0,bytes.data(),bytes.size());
  // The POST writer is an FPGA HP/PS-DDR master. Use the SDK's PS udma
  // region so the CPU mapping is coherent with FPGA writes; defaultMemRegion
  // is the NPU/native region and can leave a stale cached readback here.
  auto result_mem=dev.getMemRegion("udma").malloc(448,false); auto result=(result_mem->begin.addr()+63)&~uint64_t(63); if(result>=0x40000000ULL) throw runtime_error("udma result allocation is not PS-visible"); vector<char> zero(448,0); result_mem.write(result-result_mem->begin.addr(),zero.data(),zero.size()); cerr<<"POST_V2_RESULT_ADDR=0x"<<hex<<result<<dec<<"\n";
  unsigned n=argc>3?stoul(argv[3]):1; for(unsigned frame=1;frame<=n;++frame){ auto outs=session.forward({input}); if(outs.size()!=6) throw runtime_error("model did not return six outputs"); xunjing::postv2::Job j; j.frame=frame; j.result=uint32_t(result); uint64_t lo=~0ULL, hi=0; vector<vector<char>> raw;
    const unsigned bytes_expected[6]={14080,56320,3520,14080,880,3520}; for(unsigned i=0;i<6;++i){ raw.push_back(dump(outs[i])); if(raw.back().size()!=bytes_expected[i]) throw runtime_error("unexpected output bytes"); auto a=outs[i].data().addr(); if(a>0xffffffffULL) throw runtime_error("native output address exceeds 32-bit PL interface"); j.tensor[i]={uint32_t(a),uint32_t(raw.back().size()),uint32_t(i&1?16:4),4}; lo=min(lo,a&~uint64_t(63)); hi=max(hi,(a+raw.back().size()+63)&~uint64_t(63)); }
    // Keep a CPU reference count only as an observation of this same six-way
    // NPU output; the accepted result below must come from FPGA POST V2.
    vector<xunjing::detection::Yolo26OutputView> views; views.reserve(6); const int shapes[6][4]={{1,44,80,1},{1,44,80,4},{1,22,40,1},{1,22,40,4},{1,11,20,1},{1,11,20,4}}; float max_logit=-1e30f;
    for(unsigned i=0;i<6;++i){ views.push_back({reinterpret_cast<const float*>(raw[i].data()),{1,shapes[i][1],shapes[i][2],shapes[i][3]}}); if(!(i&1)) for(size_t k=0;k<raw[i].size()/sizeof(float);++k) max_logit=max(max_logit,reinterpret_cast<const float*>(raw[i].data())[k]); }
    size_t cpu_candidates=0; auto cpu_ref=xunjing::detection::DecodeYolo26Conv(views,1,.5,.6,300,&cpu_candidates); cerr<<"NPU_MAX_LOGIT="<<max_logit<<" CPU_CANDIDATES="<<cpu_candidates<<" CPU_NMS_COUNT="<<cpu_ref.size()<<"\n";
    j.arena=uint32_t(lo); j.arena_bytes=uint32_t(hi-lo); cerr<<"POST_V2_ARENA=0x"<<hex<<j.arena<<"+0x"<<j.arena_bytes<<dec<<"\n"; auto begin=chrono::steady_clock::now(); ctl.submit(j); ctl.wait(chrono::milliseconds(4000)); auto end=chrono::steady_clock::now(); vector<char> rb(448); result_mem.read(rb.data(),result-result_mem->begin.addr(),rb.size()); xunjing::postv2::Result out{}; memcpy(&out,rb.data(),sizeof(out)); if(out.count>32) throw runtime_error("result count is invalid (buffer not committed or PS cache not invalidated): "+to_string(out.count)); cout<<"{\"frame\":"<<out.frame_id<<",\"count\":"<<out.count<<",\"status\":"<<ctl.status()<<",\"pl_wait_ms\":"<<chrono::duration<double,milli>(end-begin).count()<<",\"boxes\":["; for(unsigned i=0;i<out.count;++i){if(i)cout<<','; cout<<"["<<out.box[i][0]<<','<<out.box[i][1]<<','<<out.box[i][2]<<','<<out.box[i][3]<<","<<fixed<<setprecision(6)<<out.score[i]/32768.0<<","<<out.class_id[i]<<"]";} cout<<"]}\n"; dev.reset(1);
  } return 0;
 }catch(const exception&e){cerr<<"POST_V2_ACCEPTANCE_ERROR: "<<e.what()<<'\n'; return 1;} }
