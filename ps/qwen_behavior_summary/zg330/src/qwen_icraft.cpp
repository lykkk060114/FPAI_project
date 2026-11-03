#include <icraft-utils/logging.h>
#include <icraft-xir/core/network.h>
#include <icraft-xrt/core/session.h>
#include <icraft-xrt/core/tensor.h>
#include <icraft-xrt/dev/host_device.h>
#include <icraft-xrt/dev/zg330_device.h>
#include <icraft-backends/hostbackend/backend.h>
#include <icraft-backends/zg330backend/zg330backend.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace icraft::xrt;
using Clock=std::chrono::steady_clock;
static double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static std::string memoryStatus(){
 std::ifstream file("/proc/self/status");std::string line,result;
 while(std::getline(file,line))if(line.rfind("VmRSS:",0)==0||line.rfind("VmHWM:",0)==0)result+=line+" ";
 return result;
}
static double cpuMs(){timespec t{};clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
static std::uint64_t deviceBytes(const Device& device){
 std::uint64_t bytes=0;for(const auto& chunk:device.defaultMemRegion().memManager().getAllMemChunk())bytes+=chunk->byte_size;return bytes;
}
struct MappedParameters {
 char* data=nullptr;std::size_t bytes=0;
 explicit MappedParameters(const std::string& path){
  const int fd=open(path.c_str(),O_RDONLY);if(fd<0)throw std::runtime_error("parameter file open failed");
  struct stat st{};if(fstat(fd,&st)||st.st_size<=0){close(fd);throw std::runtime_error("parameter file size failed");}
  bytes=st.st_size;void* address=mmap(nullptr,bytes,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
  if(address==MAP_FAILED)throw std::runtime_error("parameter mmap failed");data=static_cast<char*>(address);
 }
 ~MappedParameters(){if(data)munmap(data,bytes);}
 void DropResidentPages(){if(madvise(data,bytes,MADV_DONTNEED))throw std::runtime_error("parameter madvise failed");}
};
// The SDK owns restored mappings. Release only clean, read-only file pages;
// keep their addresses valid for lazy parameter references and Session teardown.
static void dropSnapshotPages(const std::string& path){
 const auto absolute=std::filesystem::canonical(path).string();
 std::ifstream maps("/proc/self/maps");std::string line;std::uint64_t dropped=0;
 while(std::getline(maps,line)){
  std::istringstream fields(line);std::string range,permissions,offset,dev,inode,name;
  fields>>range>>permissions>>offset>>dev>>inode;std::getline(fields,name);
  const auto start=name.find_first_not_of(' ');if(start==std::string::npos||name.substr(start)!=absolute)continue;
  if(permissions.size()<2||permissions[1]=='w')continue;
  const auto dash=range.find('-');auto begin=std::stoull(range.substr(0,dash),nullptr,16),end=std::stoull(range.substr(dash+1),nullptr,16);
  if(madvise(reinterpret_cast<void*>(begin),end-begin,MADV_DONTNEED))throw std::runtime_error("snapshot madvise failed");dropped+=end-begin;
 }
 std::cerr<<"[QWEN_SNAPSHOT] readonly_mapping_bytes_advised="<<dropped<<std::endl;
}
struct Graph {
 Device device; std::shared_ptr<MappedParameters> mapped; Session session; Tensor input; bool zg;
 Graph(const std::string& prefix,Device d,bool hardware,const std::string& snapshotDir="",const std::string& snapshotMode="off"):device(d),zg(hardware){
  auto started=Clock::now();
  auto report=[&](const char* phase){std::cerr<<"[QWEN_LOAD] "<<phase<<" elapsed_ms="<<elapsed(started)<<" model="<<prefix<<" "<<memoryStatus()<<std::endl;};
  const std::string snapshot=snapshotDir.empty()?"":snapshotDir+"/"+std::filesystem::path(prefix).filename().string()+".snapshot";
  if(zg&&snapshotMode=="read"){
   session=Session::CreateWithSnapshot(snapshot,{icraft::xrt::zg330::ZG330Backend::Init(),HostBackend::Init()},{device,HostDevice::Default()});report("snapshot_restored");
  }else{
  auto net=icraft::xir::Network::CreateFromJsonFile(prefix+"_ZG.json");report("json_parsed");
  mapped=std::make_shared<MappedParameters>(prefix+"_ZG.raw");
  net.lazyLoadParams(mapped->data,mapped->bytes);report("params_bound");
  auto view=net.viewExcept({0,1});
  if(zg){
   session=Session::Create<icraft::xrt::zg330::ZG330Backend,HostBackend>(view,{device,HostDevice::Default()});
   report("session_created");
   auto backend=session->backends[0].cast<icraft::xrt::zg330::ZG330Backend>();
   backend.disableEtmOptimize();backend.disableMergeHardOp();session.apply();report("session_applied");
   if(snapshotMode=="write"){
    std::filesystem::create_directories(snapshotDir);session.dumpSnapshot(snapshot+".pending");std::filesystem::rename(snapshot+".pending",snapshot);report("snapshot_written");
   }
  }else{
   session=Session::Create<HostBackend>(view,{device});session.apply();
   auto op=std::get<0>(session.getForwards().at(0));input=Tensor(op->inputs[0]);input.mallocOn(HostDevice::MemRegion());
  }
  }
  if(zg){
   auto backend=session->backends[0].cast<icraft::xrt::zg330::ZG330Backend>();
   const auto& inputs=session->network_view.inputs();
   if(inputs.size()!=1)throw std::runtime_error("Qwen graph requires exactly one input");
   auto vid=inputs[0]->v_id;
   auto info=backend->forward_info->value_map.at(vid);auto chunk=backend->forward_info->memchunk_map.at(vid)->memChunk;
   input=Tensor(info->value);input.setData(chunk,info->phy_addr-chunk->begin.addr());
   if(mapped){mapped->DropResidentPages();report("host_param_pages_dropped");}
   if(snapshotMode=="read"){dropSnapshotPages(snapshot);report("snapshot_host_pages_dropped");}
  }
 }
 std::vector<float> run(const std::vector<float>& values){
  if(input.dtype().bytes()!=values.size()*4)throw std::runtime_error("model/input byte contract mismatch");
  if(zg){auto b=session->backends[0].cast<icraft::xrt::zg330::ZG330Backend>();if(b->map_table.defined()){auto dev=device.cast<ZG330Device>();dev.updateMMU(b->map_table);dev.clearMMUCache();}}
  input.write(0,reinterpret_cast<char*>(const_cast<float*>(values.data())),values.size()*4);
  auto output=session.forward({input});
  if(output.size()!=1 || !output[0].waitForReady(std::chrono::milliseconds(30000)))throw std::runtime_error("Qwen output timeout");
  std::ostringstream stream;output[0].dump(stream,"SFB");auto bytes=stream.str();
  if(bytes.size()%4)throw std::runtime_error("non-FP32 output");
  std::vector<float> result(bytes.size()/4);std::memcpy(result.data(),bytes.data(),bytes.size());
  for(auto v:result)if(!std::isfinite(v))throw std::runtime_error("non-finite Qwen output");
  if(zg)device.reset(1);return result;
 }
};
struct Cache {std::vector<float> k,v;explicit Cache(int c):k(2*c*64),v(2*c*64){}};
int main(int argc,char** argv){try{
 std::string root,tokens,output,timing,backend="zg330",snapshotDir,snapshotMode="off";int context=512,limit=128,probeLayers=0,probeRepeats=3,prefillBlock=16;bool resident=false;
 for(int i=1;i<argc;++i){std::string k=argv[i];if(k=="--resident"){resident=true;continue;}if(i+1>=argc)throw std::runtime_error("missing argument");std::string v=argv[++i];
 if(k=="--snapshot-dir")snapshotDir=v;else if(k=="--snapshot-mode")snapshotMode=v;else if(k=="--models")root=v;else if(k=="--tokens")tokens=v;else if(k=="--output")output=v;else if(k=="--timing")timing=v;else if(k=="--backend")backend=v;else if(k=="--probe-layers")probeLayers=std::stoi(v);else if(k=="--probe-repeats")probeRepeats=std::stoi(v);else if(k=="--prefill-block")prefillBlock=std::stoi(v);else if(k=="--context")context=std::stoi(v);else if(k=="--max-new-tokens")limit=std::stoi(v);else throw std::runtime_error("unknown argument "+k);}
 if((snapshotMode!="off"&&snapshotMode!="read"&&snapshotMode!="write")||(snapshotMode!="off"&&(snapshotDir.empty()||backend!="zg330")))throw std::runtime_error("snapshot mode requires zg330 and directory, mode=off|read|write");
 if(prefillBlock!=1&&prefillBlock!=16)throw std::runtime_error("prefill block must be 1 or 16");
 if(root.empty()||tokens.empty()||output.empty()||timing.empty()||context!=512||limit<1||limit>512||(backend!="host"&&backend!="zg330"))throw std::runtime_error("Usage: qwen_icraft --models runtime --tokens prompt.ids --output generated.ids --timing timing.csv [--backend zg330|host] [--resident] [--max-new-tokens 128]");
 std::vector<int> prompt;std::ifstream ids(tokens);int id;while(ids>>id){if(id<0||id>=151936)throw std::runtime_error("token outside vocabulary");prompt.push_back(id);}
 if(prompt.empty()||prompt.size()+limit>static_cast<unsigned>(context))throw std::runtime_error("empty prompt or prompt+generation exceeds context512; input was not truncated");
 std::ifstream embeddings(root+"/embedding.bf16",std::ios::binary),rope(root+"/rope.f32",std::ios::binary);if(!embeddings||!rope)throw std::runtime_error("embedding/rope file missing");
 std::vector<float> rope_table(context*128);rope.read(reinterpret_cast<char*>(rope_table.data()),rope_table.size()*4);if(!rope)throw std::runtime_error("short rope table");
 std::ofstream result(output),csv(timing);if(!result||!csv)throw std::runtime_error("output file unavailable");csv<<"position,phase,graph,load_ms,write_forward_read_ms,wall_ms,process_cpu_ms,input_bytes,output_bytes,backend\n";
 const bool zg=backend=="zg330";Device device=zg?Device::Open("axi://zg330aiu?npu=0x40000000&dma=0x80000000"):HostDevice::Default();
 const auto& regionInfo=device.defaultMemRegion().memManager().getMemRegionInfo();
 auto sizeInfo=regionInfo.find("byte_size");if(sizeInfo!=regionInfo.end()){auto size=sizeInfo->second.as<icraft::xir::IntImmNode>();if(size)std::cerr<<"[QWEN_MEMORY] device_region_bytes="<<size->value<<std::endl;}
 std::vector<std::unique_ptr<Graph>> graphs(49);std::vector<Cache> caches;for(int i=0;i<24;++i)caches.emplace_back(context);
 const int rows=(context+64)/64;const int count=(16+rows+4*context)*64;
 std::vector<float> packed(count),hidden(896);std::vector<std::uint16_t> row(896);unsigned generated=0;int token=prompt[0];auto total=Clock::now();
 if(probeLayers){
  if(probeLayers<1||probeLayers>24||probeRepeats<2||probeRepeats>10)throw std::runtime_error("probe limits invalid");
  std::vector<std::vector<float>> references(probeLayers);
  for(int repeat=0;repeat<probeRepeats;++repeat)for(int layer=0;layer<probeLayers;++layer){
   std::ostringstream n;n<<"decoder_"<<std::setfill('0')<<std::setw(2)<<layer<<"_c512";
   auto started=Clock::now();bool loaded=!graphs[layer];if(loaded)graphs[layer]=std::make_unique<Graph>(root+"/"+n.str(),device,zg,snapshotDir,snapshotMode);
   const double load=loaded?elapsed(started):0.;auto infer=Clock::now();
   std::vector<float> x(132672,0.f);auto y=graphs[layer]->run(x);const double compute=elapsed(infer);
   double maximum=0.;if(repeat==0){references[layer]=y;result<<std::setprecision(9);for(float v:y)result<<v<<'\n';result.flush();}else {if(y.size()!=references[layer].size())throw std::runtime_error("reuse output length changed");for(size_t j=0;j<y.size();++j)maximum=std::max(maximum,std::abs(double(y[j])-references[layer][j]));}
   csv<<repeat<<",resident_probe,"<<n.str()<<','<<load<<','<<compute<<','<<elapsed(started)<<','<<0<<','<<x.size()*4<<','<<y.size()*4<<','<<backend<<std::endl;
   std::uint64_t bytes=0;auto chunks=device.defaultMemRegion().memManager().getAllMemChunk();for(auto& chunk:chunks)bytes+=chunk->byte_size;
   std::cerr<<"[QWEN_REUSE] repeat="<<repeat<<" layer="<<layer<<" load_ms="<<load<<" compute_ms="<<compute<<" max_abs="<<maximum<<" device_allocated_bytes="<<bytes<<" chunks="<<chunks.size()<<" "<<memoryStatus()<<std::endl;
   if(maximum>1e-5)throw std::runtime_error("resident output mismatch after session switch");
  }
  return 0;
 }
 unsigned pos=0;
 while(pos<prompt.size()+limit-1){
  auto step=Clock::now();
  const unsigned valid=pos<prompt.size()?std::min<unsigned>(prefillBlock,prompt.size()-pos):1;
  const int block=valid>1?16:1;
  const int maskRows=(context+block+63)/64;
  const int off=(16*block+maskRows*block)*64;
  packed.assign(off+4*context*64,0);hidden.assign(block*896,0);
  for(unsigned b=0;b<valid;++b){
   const int current=pos+b<prompt.size()?prompt[pos+b]:token;
   embeddings.seekg(static_cast<std::streamoff>(current)*896*2);embeddings.read(reinterpret_cast<char*>(row.data()),1792);if(!embeddings)throw std::runtime_error("short embedding row");
   for(int j=0;j<896;++j){std::uint32_t word=std::uint32_t(row[j])<<16;std::memcpy(&hidden[b*896+j],&word,4);}
  }
  auto execute=[&](unsigned index,const std::vector<float>& x){
   auto started=Clock::now();const double cpuStart=cpuMs();bool loaded=false;std::string name;
   const unsigned slot=index<24?(block==16?index+25:index):24;
   if(index<24){std::ostringstream n;n<<"decoder_"<<std::setfill('0')<<std::setw(2)<<index<<"_c512";if(block==16)n<<"_b16";name=n.str();}else name="head";
   if(!graphs[slot]){graphs[slot]=std::make_unique<Graph>(root+"/"+name,device,zg,snapshotDir,snapshotMode);loaded=true;}double load=loaded?elapsed(started):0;auto infer=Clock::now();auto y=graphs[slot]->run(x);double compute=elapsed(infer);
   csv<<pos<<','<<(pos+valid<prompt.size()?"prefill":"decode")<<','<<name<<','<<load<<','<<compute<<','<<elapsed(started)<<','<<cpuMs()-cpuStart<<','<<x.size()*4<<','<<y.size()*4<<','<<backend<<'\n';csv.flush();
   if(!resident)graphs[slot].reset();return y;
  };
  for(int layer=0;layer<24;++layer){
   std::fill(packed.begin(),packed.end(),0);std::copy(hidden.begin(),hidden.end(),packed.begin());
   for(int b=0;b<block;++b){const auto rp=std::min<unsigned>(pos+b,context-1);std::copy_n(rope_table.begin()+rp*128,64,packed.begin()+896*block+b*64);std::copy_n(rope_table.begin()+rp*128+64,64,packed.begin()+960*block+b*64);
    for(int t=0;t<context+block;++t)packed[1024*block+b*maskRows*64+t]=(t<int(pos)||(t>=context&&t-context<=b))?0.f:-10000.f;
   }
   std::copy(caches[layer].k.begin(),caches[layer].k.end(),packed.begin()+off);std::copy(caches[layer].v.begin(),caches[layer].v.end(),packed.begin()+off+2*context*64);
   auto y=execute(layer,packed);if(y.size()!=unsigned(block*1152))throw std::runtime_error("decoder output length mismatch");std::copy_n(y.begin(),block*896,hidden.begin());
   for(int g=0;g<2;++g)for(unsigned b=0;b<valid;++b){std::copy_n(y.begin()+896*block+(g*block+b)*64,64,caches[layer].k.begin()+(g*context+pos+b)*64);std::copy_n(y.begin()+1024*block+(g*block+b)*64,64,caches[layer].v.begin()+(g*context+pos+b)*64);}
  }
  if(pos+valid>=prompt.size()){
   if(resident)for(unsigned i=25;i<graphs.size();++i)graphs[i].reset();
   std::vector<float> last(hidden.begin()+(valid-1)*896,hidden.begin()+valid*896);auto logits=execute(24,last);if(logits.size()!=151936)throw std::runtime_error("head output length mismatch");token=std::max_element(logits.begin(),logits.end())-logits.begin();result<<token<<'\n';result.flush();++generated;std::cerr<<"[QWEN "<<backend<<"] token="<<token<<" position="<<pos<<" valid="<<valid<<" wall_ms="<<elapsed(step)<<'\n';if(token==151645||token==151643||generated>=static_cast<unsigned>(limit))break;
  }
  pos+=valid;
 }
 std::cerr<<"[QWEN] generated="<<generated<<" total_ms="<<elapsed(total)<<" backend="<<backend<<" streaming="<<!resident<<" device_allocated_bytes="<<deviceBytes(device)<<" "<<memoryStatus()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"QWEN_ERROR: "<<e.what()<<'\n';return 1;}}
