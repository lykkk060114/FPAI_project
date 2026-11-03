// Fixed-input ZG330 comparison: no camera writes and no classifier session.
#include <icraft-utils/logging.h>
#include "icraft_utils.hpp"
#include "compile_fpai_target.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <algorithm>
int main(int argc,char**argv){try{
 if(argc!=4)throw std::runtime_error("Usage: detector_replay model_dir inputs_dir output_dir");
 namespace fs=std::filesystem;using Clock=std::chrono::steady_clock;
 fs::path models=argv[1],inputs=argv[2],output=argv[3];fs::create_directories(output);
 std::vector<fs::path> cases;for(auto&e:fs::directory_iterator(inputs))if(e.path().extension()==".f32")cases.push_back(e.path());std::sort(cases.begin(),cases.end());if(cases.empty())throw std::runtime_error("No FP32 inputs");
 auto device=icraft::xrt::Device::Open("axi://zg330aiu?npu=0x40000000&dma=0x80000000");
 auto net=loadNetwork((models/"yolo26_panda_conv_ZG.json").string(),(models/"yolo26_panda_conv_ZG.raw").string());auto session=initSession("zg330",net.viewExcept({0,1}),device,-1,true,false,false);session.apply();
 auto backend=session->backends[0].cast<icraft::xrt::zg330::ZG330Backend>();auto op=std::get<0>(session.getForwards().at(0));auto id=op->inputs[0]->v_id;auto info=backend->forward_info->value_map.at(id);auto chunk=backend->forward_info->memchunk_map.at(id)->memChunk;
 icraft::xrt::Tensor input(info->value);input.setData(chunk,info->phy_addr-chunk->begin.addr());
 auto shape=input.dtype()->shape;
 if(shape.size()!=4||shape[0]!=1||shape[1]!=352||shape[2]!=640||shape[3]!=3||!input.dtype()->element_dtype.getStorageType().isFP32())throw std::runtime_error("Wrong input contract");
 std::ofstream timing(output/"timing.csv");timing<<"case,repeat,write_ms,forward_wait_ms,read_ms\n";
 for(auto&path:cases){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=2703360)throw std::runtime_error("Wrong input size");f.seekg(0);std::vector<float> data(675840);f.read(reinterpret_cast<char*>(data.data()),2703360);if(!f||!std::all_of(data.begin(),data.end(),[](float x){return std::isfinite(x)&&x>=0&&x<=255;}))throw std::runtime_error("Invalid BGR values");
  auto dir=output/path.stem();fs::create_directories(dir);
  for(int repeat=0;repeat<13;++repeat){auto a=Clock::now();input.write(0,reinterpret_cast<char*>(data.data()),2703360);auto b=Clock::now();auto outs=session.forward({input});if(outs.size()!=6)throw std::runtime_error("Expected six tensors");for(auto&t:outs)if(!t.waitForReady(std::chrono::seconds(10)))throw std::runtime_error("NPU timeout");auto c=Clock::now();
   if(repeat==12){const size_t expected[]={14080,56320,3520,14080,880,3520};for(size_t k=0;k<6;++k){std::ostringstream ss;outs[k].dump(ss,"SFB");auto bytes=ss.str();if(bytes.size()!=expected[k])throw std::runtime_error("Wrong output bytes");std::ofstream out(dir/("output_"+std::to_string(k)+".f32"),std::ios::binary);out.write(bytes.data(),bytes.size());if(!out)throw std::runtime_error("Output write failed");}}
   auto e=Clock::now();if(repeat>=3)timing<<path.stem().string()<<','<<repeat<<','<<std::chrono::duration<double,std::milli>(b-a).count()<<','<<std::chrono::duration<double,std::milli>(c-b).count()<<','<<std::chrono::duration<double,std::milli>(e-c).count()<<'\n';device.reset(1);
  }
 }
 std::cout<<"replayed="<<cases.size()<<" measured_repeats_each=10\n";return 0;
}catch(const std::exception&e){std::cerr<<"REPLAY_ERROR "<<e.what()<<'\n';return 1;}}
