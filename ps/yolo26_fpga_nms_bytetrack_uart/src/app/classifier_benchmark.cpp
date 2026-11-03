#include <icraft-utils/logging.h>
#include "icraft_utils.hpp"
#include "ZgClassification.hpp"
#include <onnxruntime_cxx_api.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <array>
#include <cmath>
int main(int argc,char** argv){
 try {
  if(argc!=8)throw std::runtime_error("Usage: classifier_benchmark cpu|zg330 model.json|onnx model.raw|- input_rgb_nhwc.f32 result.csv repeats warmup");
  std::string mode=argv[1];if(mode!="cpu"&&mode!="zg330")throw std::runtime_error("Invalid backend");
  int count=std::stoi(argv[6]),warmup=std::stoi(argv[7]);if(count<1||count>10000||warmup<0||warmup>1000)throw std::runtime_error("Invalid repeats");
  std::ifstream in(argv[4],std::ios::binary|std::ios::ate);if(!in||in.tellg()!=602112)throw std::runtime_error("Expected 602112-byte normalized RGB NHWC input");
  std::vector<float> nhwc(150528),nchw(150528);in.seekg(0);in.read(reinterpret_cast<char*>(nhwc.data()),602112);
  if(!in||!std::all_of(nhwc.begin(),nhwc.end(),[](float v){return std::isfinite(v);}))throw std::runtime_error("Invalid input");
  for(size_t i=0;i<50176;++i)for(size_t c=0;c<3;++c)nchw[c*50176+i]=nhwc[i*3+c];
  auto init_start=xunjing::PerfClock::now();
  Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"benchmark");std::unique_ptr<Ort::Session> cpu;std::unique_ptr<ZgClassification> npu;
  if(mode=="cpu"){Ort::SessionOptions opts;opts.SetIntraOpNumThreads(2);opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);cpu=std::make_unique<Ort::Session>(env,argv[2],opts);}
  else {auto dev=icraft::xrt::Device::Open("axi://zg330aiu?npu=0x40000000&dma=0x80000000");npu=std::make_unique<ZgClassification>(dev,argv[2],argv[3]);}
  std::cerr<<"BENCH_INIT_MS="<<xunjing::PerfMs(init_start,xunjing::PerfClock::now())<<'\n';
  std::ofstream out(argv[5]);if(!out)throw std::runtime_error("Cannot open benchmark CSV");out<<"iteration,warmup,backend,wall_ms,process_cpu_ms,logit0,logit1,logit2,logit3,prediction\n";
  Ort::AllocatorWithDefaultOptions allocator;std::string input_name,output_name;
  if(cpu){auto i=cpu->GetInputNameAllocated(0,allocator),o=cpu->GetOutputNameAllocated(0,allocator);input_name=i.get();output_name=o.get();}
  for(int i=0;i<warmup+count;++i){auto start=xunjing::PerfClock::now();double cstart=xunjing::ProcessCpuMs();std::vector<float> values;
   if(npu)values=npu->Run(nchw);
   else {auto mem=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);std::array<int64_t,4> shape{1,3,224,224};auto input=Ort::Value::CreateTensor<float>(mem,nchw.data(),nchw.size(),shape.data(),4);const char* ins[]={input_name.c_str()};const char* outs[]={output_name.c_str()};auto outputs=cpu->Run(Ort::RunOptions{nullptr},ins,&input,1,outs,1);if(outputs[0].GetTensorTypeAndShapeInfo().GetElementCount()!=4)throw std::runtime_error("Expected four logits");values.assign(outputs[0].GetTensorData<float>(),outputs[0].GetTensorData<float>()+4);}
   double elapsed=xunjing::PerfMs(start,xunjing::PerfClock::now()),used=xunjing::ProcessCpuMs()-cstart;
   if(values.size()!=4||!std::all_of(values.begin(),values.end(),[](float v){return std::isfinite(v);}))throw std::runtime_error("Invalid logits");
   out<<i<<','<<(i<warmup)<<','<<mode<<','<<elapsed<<','<<used;for(float v:values)out<<','<<v;out<<','<<std::max_element(values.begin(),values.end())-values.begin()<<'\n';out.flush();
  }
  return 0;
 }catch(const std::exception& e){std::cerr<<"BENCH_ERROR: "<<e.what()<<'\n';return 1;}
}
