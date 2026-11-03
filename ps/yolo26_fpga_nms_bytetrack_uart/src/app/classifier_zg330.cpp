// Standalone classifier acceptance tool. Stop the SDI detector before running.
#include <icraft-utils/logging.h>
#include "icraft_utils.hpp"
#include "compile_fpai_target.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cmath>
int main(int argc,char** argv) {
 try {
  if(argc!=5) throw std::runtime_error("Usage: classifier_zg330 model.json model.raw normalized_rgb_nhwc.f32 output_logits.f32");
  std::ifstream file(argv[3],std::ios::binary|std::ios::ate);
  if(!file || file.tellg()!=602112)throw std::runtime_error("Input must be 602112 bytes: normalized FP32 RGB NHWC 1x224x224x3");
  file.seekg(0);std::vector<float> data(150528);file.read(reinterpret_cast<char*>(data.data()),602112);
  if(!file || !std::all_of(data.begin(),data.end(),[](float f){return std::isfinite(f);}))throw std::runtime_error("Invalid input");
  auto device=icraft::xrt::Device::Open("axi://zg330aiu?npu=0x40000000&dma=0x80000000");
  auto net=loadNetwork(argv[1],argv[2]);
  auto session=initSession("zg330",net.viewExcept({0,1}),device,-1,true,false,false);
  session.apply();
  auto backend=session->backends[0].cast<icraft::xrt::zg330::ZG330Backend>();
  auto first=std::get<0>(session.getForwards().at(0));
  auto vid=first->inputs[0]->v_id;
  auto info=backend->forward_info->value_map.at(vid);
  auto chunk=backend->forward_info->memchunk_map.at(vid)->memChunk;
  icraft::xrt::Tensor input(info->value);input.setData(chunk,info->phy_addr-chunk->begin.addr());
  auto shape=input.dtype()->shape;
  if(shape.size()!=4 || shape[0]!=1 || shape[1]!=224 || shape[2]!=224 || shape[3]!=3 || !input.dtype()->element_dtype.getStorageType().isFP32())throw std::runtime_error("Unexpected compiled input contract");
  input.write(0,reinterpret_cast<char*>(data.data()),602112);
  auto outputs=session.forward({input});
  if(outputs.size()!=1 || !outputs[0].waitForReady(std::chrono::milliseconds(10000)))throw std::runtime_error("NPU output missing or timeout");
  std::ostringstream stream;outputs[0].dump(stream,"SFB");auto bytes=stream.str();
  if(bytes.size()!=16)throw std::runtime_error("Expected four FP32 logits");
  float logits[4];std::memcpy(logits,bytes.data(),16);
  for(auto value:logits)if(!std::isfinite(value))throw std::runtime_error("Nonfinite logits");
  std::ofstream output(argv[4],std::ios::binary);output.write(bytes.data(),bytes.size());output.close();if(!output)throw std::runtime_error("Output write failed");
  const char* labels[]={"drink","eat","rest","walk"};
  for(int i=0;i<4;++i)std::cout<<labels[i]<<": "<<logits[i]<<'\n';
  std::cout<<"class="<<labels[std::max_element(logits,logits+4)-logits]<<'\n';
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
