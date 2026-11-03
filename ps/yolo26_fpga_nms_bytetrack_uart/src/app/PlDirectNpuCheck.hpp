#pragma once
#include "PlInputBridge.hpp"
#include <sstream>
#include <cstdlib>
#include <algorithm>
namespace xunjing {
// Optional fixed-input experiment: compare direct PL input inference against
// SDK Tensor.write of the exact same bytes. Disabled in performance runs.
class PlDirectNpuCheck {
 std::vector<char> raw_;
 bool enabled_=false;
 template<class Tensor> static std::string Dump(Tensor& t){if(!t.waitForReady(std::chrono::milliseconds(10000)))throw std::runtime_error("NPU check output timeout");std::ostringstream o;t.dump(o,"SFB");return o.str();}
public:
 template<class Tensor> explicit PlDirectNpuCheck(Tensor& input){
  static unsigned frames=0,checks=0;
  ++frames;
  if(PlDirectEnabled()&&std::getenv("XUNJING_PL_NPU_COMPARE")&&frames>4&&checks++<4){enabled_=true;raw_.resize(2703360);}
 }
 template<class Device,class Session,class Inputs,class Outputs> void Compare(Device& device,Session& session,Inputs& inputs,Outputs& direct){
  if(!enabled_)return;
  // The reference uses SDK writes and a second forward. Keep the next native
  // writer idle for this diagnostic; normal inference is still pipelined.
  std::unique_lock<std::shared_mutex> transport(InputTransportMutex());
  std::vector<std::string> reference;for(auto& t:direct)reference.push_back(Dump(t));
  // Read AFTER direct inference: the diagnostic must not accidentally make
  // the direct input visible before the very inference being checked.
  inputs.at(0).chunk().read(raw_.data(),inputs.at(0).offset(),raw_.size());
  device.reset(1); // transport already held exclusively
  inputs.at(0).write(0,raw_.data(),raw_.size());
  auto repeated=session.forward(inputs);
  if(repeated.size()!=reference.size())throw std::runtime_error("PL/NPU comparison output count mismatch");
  double maximum=0;size_t words=0;
  for(size_t i=0;i<repeated.size();++i){auto b=Dump(repeated[i]);if(b.size()!=reference[i].size()||b.size()%4)throw std::runtime_error("PL/NPU comparison shape mismatch");for(size_t j=0;j<b.size();j+=4){float a,v;std::memcpy(&a,reference[i].data()+j,4);std::memcpy(&v,b.data()+j,4);if(!std::isfinite(a)||!std::isfinite(v))throw std::runtime_error("PL/NPU comparison nonfinite output");maximum=std::max(maximum,double(std::abs(a-v)));++words;}}
  std::cerr<<"PL_NPU_COMPARE outputs="<<repeated.size()<<" fp32_words="<<words<<" max_abs="<<maximum<<" (diagnostic SDK writeback reference only)\n";
  if(maximum>1e-4)throw std::runtime_error("Direct PL versus SDK writeback NPU outputs differ");
  direct=std::move(repeated);
 }
};
}
