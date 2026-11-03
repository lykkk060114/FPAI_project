#include <icraft-utils/logging.h>
#include "icraft_utils.hpp"
#include "compile_fpai_target.hpp"
#include "pipeline/io/input/sdicamera.hpp"
#include <csignal>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <cerrno>
namespace {volatile std::sig_atomic_t stopping=0;void stop(int){stopping=1;}}
int main(int argc,char** argv){
 try{
  if(argc!=4)throw std::runtime_error("Usage: sdi_capture fps seconds timestamps.csv; stdout is raw RGB565LE");
  double fps=std::stod(argv[1]),seconds=std::stod(argv[2]);if(!(fps>=1&&fps<=30)||!(seconds>=0&&seconds<=86400))throw std::runtime_error("Invalid duration/fps");
  std::signal(SIGINT,stop);std::signal(SIGTERM,stop);std::signal(SIGPIPE,SIG_IGN);
  int video_fd=dup(STDOUT_FILENO);if(video_fd<0)throw std::runtime_error("Cannot duplicate video pipe");
  fflush(stdout);dup2(STDERR_FILENO,STDOUT_FILENO);
  std::cout.rdbuf(std::cerr.rdbuf());spdlog::set_default_logger(spdlog::stderr_color_mt("capture"));
  auto device=icraft::xrt::Device::Open("axi://zg330aiu?npu=0x40000000&dma=0x80000000");
  auto dev=device.cast<fpai::FPAIDevice>();
  fpai::SDICamera<fpai::FPAIDevice> camera(0,dev,1920,1080,camera_fmt::RGB565,1920,1080,camera_fmt::RGB565,crop_position::center,640,352,crop_position::center);
  auto chunk=device.getMemRegion("udma").malloc(1920*1080*2,false);
  std::vector<char> pixels(1920*1080*2);std::ofstream csv(argv[3]);if(!csv)throw std::runtime_error("Cannot open timestamp CSV");
  csv<<"attempt,frame_index,request_steady_ns,done_steady_ns,capture_ms,read_ms,pipe_write_ms,ok\n";
  using Clock=std::chrono::steady_clock;auto start=Clock::now(),next=start;size_t frame=0,attempt=0,errors=0;
  fflush(stdout);dup2(video_fd,STDOUT_FILENO);close(video_fd);
  while(!stopping){
   auto t=Clock::now();if(seconds>0&&std::chrono::duration<double>(t-start).count()>=seconds)break;
   camera.take(chunk);bool ok=camera.wait(100);auto done=Clock::now();auto read=done,end=done;
   if(ok){chunk.read(pixels.data(),0,pixels.size());read=Clock::now();size_t offset=0;
    while(offset<pixels.size()){auto n=::write(STDOUT_FILENO,pixels.data()+offset,pixels.size()-offset);if(n<0){if(errno==EINTR)continue;throw std::runtime_error("Video encoder pipe closed");}if(n==0)throw std::runtime_error("Empty pipe write");offset+=n;}
    end=Clock::now();
   }else ++errors;
   auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
   csv<<attempt++<<','<<(ok?static_cast<long>(frame++):-1)<<','<<std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count()<<','<<std::chrono::duration_cast<std::chrono::nanoseconds>(done.time_since_epoch()).count()<<','<<ms(t,done)<<','<<ms(done,read)<<','<<ms(read,end)<<','<<ok<<'\n';csv.flush();
   if(errors>=10&&frame==0)throw std::runtime_error("No camera frames: check camera power and SDI_IN_0");
   next+=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1/fps));if(next<Clock::now())next=Clock::now();std::this_thread::sleep_until(next);
  }
  std::cerr<<"CAPTURE frames="<<frame<<" errors="<<errors<<" elapsed_s="<<std::chrono::duration<double>(Clock::now()-start).count()<<'\n';return frame?0:2;
 }catch(const std::exception& e){std::cerr<<"CAPTURE_ERROR: "<<e.what()<<'\n';return 1;}
}
