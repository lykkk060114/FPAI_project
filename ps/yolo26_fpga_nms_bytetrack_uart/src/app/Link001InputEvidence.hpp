#pragma once
// Optional diagnostic only. No Tensor.write(), no FPGA register writes, no NPU start.
// Use the same SDK3.36.1 that built the deployed actor; local SDK3.33.1 is only a syntax check.
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>

namespace xunjing::link001 {
inline std::string Quote(const std::string& s) {
 std::ostringstream o;o<<'"';
 for(unsigned char c:s) {
  if(c=='"'||c=='\\')o<<'\\'<<char(c);
  else if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c);
  else o<<char(c);
 }
 o<<'"';return o.str();
}
inline void Bytes(const std::filesystem::path& p,const void* data,std::size_t count) {
 std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot create "+p.string());
 f.write(static_cast<const char*>(data),count);f.close();if(!f)throw std::runtime_error("incomplete write "+p.string());
}
template<class Device> auto MmuMode(Device& d,int)->decltype(d.getMMUMode(),int{}) {return d.getMMUMode()?1:0;}
template<class Device> int MmuMode(Device&,long) {return -1;}

// Read-only SDK metadata snapshot; never converts an address to a PL bus address.
template<class Backend>
void CaptureMapping(const Backend& backend,std::int64_t value_id,unsigned buffer_index,int source_id) noexcept {
 const char* root=std::getenv("XUNJING_LINK_MAPPING_DIR");if(!root||!*root)return;
 try {
  static std::mutex guard;static std::set<std::pair<int,unsigned>> attempted;
  {std::lock_guard<std::mutex> lock(guard);if(attempted.size()>=4||!attempted.emplace(source_id,buffer_index).second)return;}
  const auto dir=std::filesystem::path(root)/("mapping_"+std::to_string(source_id)+"_"+std::to_string(buffer_index));
  if(!std::filesystem::create_directories(dir))throw std::runtime_error("mapping capture exists; use new root");
  const auto v=backend->forward_info->value_map.at(value_id);
  std::ofstream f(dir/"mapping.pending.json");
  f<<"{\n\"scope\":\"SDK metadata only; User_Ddr address translation and fence unproven\",\n"
   <<"\"source_id\":"<<source_id<<",\n\"buffer_index\":"<<buffer_index<<",\n\"value_id\":"<<value_id
   <<",\n\"value_logic_addr_decimal\":"<<Quote(std::to_string(v->logic_addr))
   <<",\n\"value_phy_addr_decimal\":"<<Quote(std::to_string(v->phy_addr))
   <<",\n\"value_bytes\":"<<v->byte_size<<",\n\"is_host\":"<<(v->is_host?"true":"false")
   <<",\n\"is_ocm\":"<<(v->is_ocm?"true":"false")
   <<",\n\"backend_alloc_mode_numeric\":"<<static_cast<int>(backend->alloc_mode)<<",\n\"mmu_table_present\":"<<(backend->map_table.defined()?"true":"false");
  if(backend->map_table.defined()) {
   const auto table=backend->map_table;
   f<<",\n\"table_chunk_addr_decimal\":"<<Quote(std::to_string(table->chunk_addr))
    <<",\n\"table_bytes\":"<<table->bytesize<<",\n\"max_id\":"<<table->max_id<<",\n\"entries\":[";
   bool first=true;for(const auto& entry:table->map_table){if(!first)f<<',';first=false;
    f<<"{\"chunk_id\":"<<entry.first<<",\"sdk_mapped_address_decimal\":"<<Quote(std::to_string(entry.second))<<'}';}
   f<<"]";
  }
  f<<",\n\"user_ddr_translation_proven\":false,\n\"complete\":true\n}\n";
  f.close();if(!f)throw std::runtime_error("mapping metadata write failed");
  std::filesystem::rename(dir/"mapping.pending.json",dir/"mapping.json");
 }catch(const std::exception& e){std::cerr<<"[LINK-001 mapping evidence] "<<e.what()<<'\n';}
}

template<class Tensor,class Device>
bool Capture(const Tensor& tensor,const std::vector<float>& cpu_values,
 std::int64_t value_id,std::uint64_t forward_phy_addr,std::uint64_t forward_storage_bytes,
 std::uint64_t image_make_base,unsigned buffer_index,int source_id,
 const std::string& timestamp,Device& device) noexcept {
 const char* root=std::getenv("XUNJING_LINK_INPUT_REFERENCE_DIR");
 if(!root||!*root)return false;
 try {
  static std::mutex guard;static std::set<std::pair<int,unsigned>> reserved;
  {std::lock_guard<std::mutex> lock(guard);if(reserved.size()>=4||!reserved.emplace(source_id,buffer_index).second)return false;}
  static_assert(sizeof(float)==4,"FP32 required");
  std::uint16_t endian=1;if(*reinterpret_cast<unsigned char*>(&endian)!=1)throw std::runtime_error("requires little endian");
  if(!tensor.dtype()->element_dtype.getStorageType().isFP32())throw std::runtime_error("input is not FP32");
  const std::vector<std::int64_t> expected_shape{1,352,640,3};
  if(tensor.dtype()->shape.size()!=expected_shape.size())throw std::runtime_error("input shape mismatch");
  for(std::size_t i=0;i<expected_shape.size();++i)if(tensor.dtype()->shape[i]!=expected_shape[i])throw std::runtime_error("input shape mismatch");
  const auto chunk=tensor.chunk();const auto off=tensor.offset();
  const std::uint64_t available=chunk->byte_size;
  if(cpu_values.size()!=675840 || forward_storage_bytes==0 || forward_storage_bytes>64ULL*1024*1024)
   throw std::runtime_error("missing/unsupported input runtime storage size");
  if(off>available||forward_storage_bytes>available-off)throw std::runtime_error("storage exceeds allocation");
  if(tensor.data().addr()!=forward_phy_addr||chunk->begin.addr()+off!=forward_phy_addr)
   throw std::runtime_error("input tensor/forward_info address mismatch");
  const auto dir=std::filesystem::path(root)/("input_"+std::to_string(source_id)+"_buffer_"+std::to_string(buffer_index));
  if(!std::filesystem::create_directories(dir))throw std::runtime_error("capture exists; choose a new root");
  std::vector<char> raw(static_cast<std::size_t>(forward_storage_bytes));
  // Raw MemChunk bytes: deliberately no Tensor.to(), dump("SFB"), conversion or reinterpretation.
  chunk.read(raw.data(),off,forward_storage_bytes);
  const auto logical_bytes=cpu_values.size()*sizeof(float);
  const bool same_size=raw.size()==logical_bytes;
  const bool equal=same_size&&std::memcmp(raw.data(),cpu_values.data(),logical_bytes)==0;
  Bytes(dir/"input_raw_memchunk.bin",raw.data(),raw.size());
  Bytes(dir/"cpu_input.f32le.bin",cpu_values.data(),logical_bytes);
  std::ofstream meta(dir/"capture.pending.json");if(!meta)throw std::runtime_error("metadata open failed");
  meta<<"{\n\"scope\":\"raw SDK MemChunk read after existing CPU input write; PL reachability and NPU visibility not proven\",\n"
      <<"\"value_id\":"<<value_id<<",\n\"source_id\":"<<source_id<<",\n\"buffer_index\":"<<buffer_index<<",\n\"source_timestamp\":"<<Quote(timestamp)<<",\n"
      <<"\"region_type\":"<<Quote(std::string(tensor.memRegion()->typeKey()))<<",\n"
      <<"\"tensor_data_addr_decimal\":"<<Quote(std::to_string(tensor.data().addr()))<<",\n"
      <<"\"forward_phy_addr_decimal\":"<<Quote(std::to_string(forward_phy_addr))<<",\n"
      <<"\"forward_storage_bytes\":"<<forward_storage_bytes<<",\n"
      <<"\"chunk_begin_decimal\":"<<Quote(std::to_string(chunk->begin.addr()))<<",\n"
      <<"\"chunk_allocated_bytes\":"<<available<<",\n\"tensor_offset_bytes\":"<<off<<",\n"
      <<"\"dtype_reported_bytes\":"<<tensor.dtype().bytes()<<",\n"
      <<"\"image_make_base_decimal\":"<<Quote(std::to_string(image_make_base))<<",\n"
      <<"\"mmu_mode\":"<<MmuMode(device,0)<<",\n\"mmu_unknown_value\":-1,\n\"shape\":[";
  bool first=true;for(auto s:tensor.dtype()->shape){if(!first)meta<<',';first=false;meta<<s;}
  meta<<"],\n\"logical_bytes\":"<<logical_bytes<<",\n\"raw_equals_cpu_bytes\":"<<(equal?"true":"false")
      <<",\n\"hardware_frame_identity_proven\":false,\n\"complete\":true\n}\n";
  meta.close();if(!meta)throw std::runtime_error("metadata write failed");
  std::filesystem::rename(dir/"capture.pending.json",dir/"capture.json");
  return true;
 } catch(const std::exception& e) {std::cerr<<"[LINK-001 input evidence] "<<e.what()<<'\n';return false;}
}
}
