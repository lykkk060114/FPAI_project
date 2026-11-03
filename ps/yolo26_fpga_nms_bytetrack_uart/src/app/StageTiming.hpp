#pragma once
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <ctime>
namespace xunjing {
using PerfClock=std::chrono::steady_clock;
inline double PerfMs(PerfClock::time_point a,PerfClock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
inline double ProcessCpuMs(){timespec t{};clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
class StageTimer {
 const char* stage_;const char* backend_;long tag_;PerfClock::time_point start_=PerfClock::now();double cpu_=ProcessCpuMs();bool ok_=false;
public:
 StageTimer(const char* stage,const char* backend,long tag=-1):stage_(stage),backend_(backend),tag_(tag){}
 void Success(){ok_=true;}
 ~StageTimer(){
  const auto end=PerfClock::now();const auto cpu=ProcessCpuMs();
  try {
   static std::mutex mutex;std::lock_guard<std::mutex> lock(mutex);
   static std::ofstream file([]{const char* path=std::getenv("XUNJING_STAGE_TIMING_CSV");return path?path:"results/stage_timing.csv";}(),std::ios::app);
   if(!file)return;
   if(file.tellp()==0)file<<"steady_ns,stage,backend,tag,wall_ms,process_cpu_ms,ok\n";
   file<<std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count()<<','<<stage_<<','<<backend_<<','<<tag_<<','<<PerfMs(start_,end)<<','<<cpu-cpu_<<','<<ok_<<'\n';file.flush();
  }catch(...){}
 }
};
}
