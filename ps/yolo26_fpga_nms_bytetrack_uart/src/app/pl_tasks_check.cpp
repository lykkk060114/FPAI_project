#include "PlTasks.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <string>
#ifndef XUNJING_PL_OFFLINE_ONLY
#include <icraft-utils/logging.h>
#include "icraft_utils.hpp"
#include "compile_fpai_target.hpp"
#endif
using namespace xunjing::pl;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Fake {
    std::map<U32,U32> r;unsigned writes=0;U32 selector=0;
    std::array<U32,7> record{};
    Mmio io(){return {[this](U32 a){return a==PostBase+0x44?record.at(selector>>9):r[a];},
        [this](U32 a,U32 v){++writes;r[a]=v;if(a==PostBase+0x40)selector=v;}};}
};
template<class F> void rejects(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"expected rejection");}
void selfTest(){
    unsigned checks=0;
    const auto begin=std::chrono::steady_clock::now();
    ClassJob job{12,2,{0x100000,4147200},{0x800000,602112},3840,10,20,200,300};
    for(int failure=0;failure<10;++failure){Fake f;f.r[ClassBase]=0x584a4302;f.r[ClassBase+4]=Ready;f.r[ClassBase+0x48]=3;ClassV2 c(f.io());auto j=job;
        switch(failure){
          case 0:f.r[ClassBase]=0x584a4301;break;
          case 1:f.r[ClassBase+0x48]=1;break;
          case 2:f.r[ClassBase+4]=Ready|Busy;break;
          case 3:j.width=2000;break;
          case 4:j.destination.bytes=602111;break;
          case 5:j.destination.base+=4;break;
          case 6:j.source.bytes=4147199;break;
          case 7:j.buffer=4;break;
          case 8:j.destination.base=j.source.base;break;
          case 9:j.destination.base=0xffff0000;break;
        }
        rejects([&]{c.submit(j);});require(f.writes==0,"invalid job wrote CSR");++checks;
    }
    Fake f;f.r[ClassBase]=0x584a4302;f.r[ClassBase+4]=Ready;f.r[ClassBase+0x48]=3;ClassV2 c(f.io());c.submit(job);
    require(c.ownsBuffers()&&f.r[ClassBase+0x34]==job.destination.base&&f.r[ClassBase+0x2c]==0,"CLASS descriptor");++checks;
    f.r[ClassBase+4]=Done;f.r[ClassBase+0x40]=9408;c.wait(std::chrono::milliseconds(1));
    rejects([&]{c.finish({});});require(c.ownsBuffers(),"unfenced buffers released");++checks;
    f.r[ClassBase+0x40]=9407;rejects([&]{c.finish([]{return true;});});require(c.ownsBuffers(),"short write released");++checks;
    f.r[ClassBase+0x40]=9408;c.finish([]{return true;});require(!c.ownsBuffers(),"verified completion retained");++checks;
    for(auto status:{U32(Busy),U32(xunjing::pl::Error),U32(Rejected),U32(Done|Busy)}){
      Fake e;e.r[ClassBase]=0x584a4302;e.r[ClassBase+4]=Ready;e.r[ClassBase+0x48]=3;ClassV2 error(e.io());error.submit(job);e.r[ClassBase+4]=status;
      rejects([&]{error.wait(std::chrono::milliseconds(0));});require(error.ownsBuffers()&&error.failed(),"timeout/error ownership lost");rejects([&]{error.clear();});++checks;
    }
    PostJob p{};p.frame=1;p.buffer=0;p.arena={0x100000,0x100000};
    U32 address=p.arena.base;U32 counts[]={3520,3520,880,880,220,220};
    for(unsigned i=0;i<6;++i){const U32 bytes=counts[i]*((i%2)?16:4);p.tensors[i]={{address,bytes},(i%2)?16u:4u,4};address+=(bytes+63)&~63u;}
    Fake pf;pf.r[PostBase]=0x584a5001;pf.r[PostBase+4]=Ready;Post post(pf.io());
    auto bad=p;bad.tensors[0].memory.base-=64;rejects([&]{post.submit(bad);});require(!pf.writes,"out-of-arena descriptor wrote CSR");++checks;
    bad=p;bad.tensors[1].memory.bytes-=4;rejects([&]{post.submit(bad);});++checks;
    bad=p;bad.arena.bytes=65;rejects([&]{post.submit(bad);});++checks;
    bad=p;bad.tensors[1].cell_stride=0xfffffffc;rejects([&]{post.submit(bad);});++checks;
    post.submit(p);require(pf.r[PostBase+0x18]==0x3f19999a,"IoU confused with score threshold");++checks;
    pf.r[PostBase+4]=Done;pf.r[PostBase+0x3c]=1;pf.r[PostBase+0x48]=1;pf.record[0]=0;pf.record[1]=123;
    float values[5]={.9f,10,20,30,40};std::memcpy(pf.record.data()+2,values,sizeof(values));
    post.wait(std::chrono::milliseconds(1));auto records=post.records(std::chrono::milliseconds(5));require(records.size()==1&&records[0].index==123&&records[0].height==40,"seven-word FP32 parsing");++checks;
    pf.record[2]=0x7fc00000;rejects([&]{post.records(std::chrono::milliseconds(1));});++checks;
    std::memcpy(pf.record.data()+2,values,sizeof(values));
    pf.r[PostBase+0x3c]=301;rejects([&]{post.records(std::chrono::milliseconds(1));});++checks;
    pf.r[PostBase+0x3c]=1;pf.r[PostBase+0x48]=0;rejects([&]{post.records(std::chrono::milliseconds(0));});require(post.ownsBuffers(),"read timeout released buffers");++checks;
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"{\"mode\":\"fake_mmio\",\"checks\":"<<checks<<",\"passed\":true,\"wall_ms\":"<<ms<<"}\n";
}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--self-test"){selfTest();return 0;}
#ifndef XUNJING_PL_OFFLINE_ONLY
    if(argc==2&&std::string(argv[1])=="--probe"){
        auto start=std::chrono::steady_clock::now();
        auto device=icraft::xrt::Device::Open("axi://zg330aiu?npu=0x40000000&dma=0x80000000");auto regs=device.defaultRegRegion();
        const U32 bases[]={InputBase,PostBase,ClassBase};const U32 ids[]={0x584a4901,0x584a5001,0x584a4302};const char* names[]={"INPUT","POST","CLASS"};bool ok=true;
        std::cout<<"{\"mode\":\"read_only_probe\",\"pages\":[";
        for(unsigned i=0;i<3;++i){const auto id=regs.read(bases[i]),status=regs.read(bases[i]+4);if(i)std::cout<<',';
            std::cout<<"{\"name\":\""<<names[i]<<"\",\"base\":"<<bases[i]<<",\"id\":"<<id<<",\"expected_id\":"<<ids[i]<<",\"status\":"<<status<<",\"ready_bit\":"<<((status&Ready)?"true":"false");
            ok=ok&&(id==ids[i])&&!(status&(Busy|xunjing::pl::Error|Rejected));
            if(i==2&&id==ids[i]){const auto cap=regs.read(bases[i]+0x48);ok=ok&&cap==3;std::cout<<",\"capability\":"<<cap;}
            std::cout<<'}';
        }
        std::cout<<"],\"osd\":\"not_probed_missing_CSR_map\",\"ids_idle_ok\":"<<(ok?"true":"false")<<",\"wall_ms\":"<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"}\n";
        return ok?0:2;
    }
#endif
    std::cerr<<"Usage: pl_tasks_check --self-test | --probe (read-only; no START, RESET, or CLEAR)\n";return 1;
}catch(const std::exception& e){std::cerr<<"PL_TASK_ERROR: "<<e.what()<<'\n';return 1;}}
