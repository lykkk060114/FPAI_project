#pragma once
// Included after icraft_utils.hpp. All device access uses the common mutex.
#include "NpuExecution.hpp"
#include "StageTiming.hpp"
#include "PlTasks.hpp"
#include <memory>
#include <functional>
class ZgClassification {
    icraft::xrt::Device device_;
    icraft::xrt::Session session_;
    icraft::xrt::Tensor input_;
    // Retained across hardware failure: caller must not recycle this source.
    std::shared_ptr<void> pl_source_lease_;
public:
    ZgClassification(icraft::xrt::Device device,const std::string& json,const std::string& raw):device_(device) {
        std::lock_guard<std::recursive_mutex> guard(xunjing::NpuExecutionMutex());
        auto net=loadNetwork(json,raw);
        session_=initSession("zg330",net.viewExcept({0,1}),device_,-1,true,false,false);
        session_.apply();
        auto backend=session_->backends[0].cast<icraft::xrt::zg330::ZG330Backend>();
        auto op=std::get<0>(session_.getForwards().at(0));
        auto vid=op->inputs[0]->v_id;
        auto info=backend->forward_info->value_map.at(vid);
        auto chunk=backend->forward_info->memchunk_map.at(vid)->memChunk;
        input_=icraft::xrt::Tensor(info->value);
        input_.setData(chunk,info->phy_addr-chunk->begin.addr());
        auto shape=input_.dtype()->shape;
        if(shape.size()!=4 || shape[0]!=1 || shape[1]!=224 || shape[2]!=224 || shape[3]!=3 || !input_.dtype()->element_dtype.getStorageType().isFP32()) throw std::runtime_error("ZG classifier input contract mismatch");
    }
    std::vector<float> Run(const std::vector<float>& nchw) {
        if(nchw.size()!=150528)throw std::runtime_error("ZG classifier input size mismatch");
        xunjing::StageTimer total_timer("zg_request","zg330");
        std::vector<float> nhwc(nchw.size());
        for(size_t i=0;i<50176;++i)for(size_t c=0;c<3;++c)nhwc[3*i+c]=nchw[c*50176+i];
        auto result=Execute([&]{
            xunjing::StageTimer timer("zg_input_write","zg330");
            std::unique_lock<std::shared_mutex> transport(xunjing::InputTransportMutex());
            input_.write(0,reinterpret_cast<char*>(nhwc.data()),602112);timer.Success();
        });
        total_timer.Success();return result;
    }
    // Caller supplies a User_Ddr-addressable, same-frame, unpainted RGB565
    // buffer lease. PS udmabuf physical addresses are NOT converted here.
    // visibilityAndSample must prove device visibility and check raw NHWC data;
    // written_beats, a sleep or an MMU cache clear cannot implement this callback.
    std::vector<float> RunPl(xunjing::pl::ClassJob job,std::shared_ptr<void> sourceLease,
        const std::function<bool()>& sourceVisible,
        const std::function<bool(icraft::xrt::Tensor&,std::uint32_t,std::uint32_t)>& visibilityAndSample) {
        if(!sourceLease || !sourceVisible || !visibilityAndSample)
            throw std::invalid_argument("CLASS PL requires owned source and visibility/raw verification callbacks");
        xunjing::StageTimer timer("zg_request","pl_class_v2",job.frame);
        auto result=Execute([&]{
            std::unique_lock<std::shared_mutex> transport(xunjing::InputTransportMutex());
            const auto addr=input_.data().addr();const auto bytes=input_.dtype().bytes();
            if(addr>0xffffffffULL || bytes!=602112)
                throw std::runtime_error("CLASS runtime tensor not native32 contiguous FP32 NHWC");
            job.destination={static_cast<std::uint32_t>(addr),static_cast<std::uint32_t>(bytes)};
            auto regs=device_.defaultRegRegion();
            xunjing::pl::ClassV2 control({[&](std::uint32_t a){return regs.read(a);},
                [&](std::uint32_t a,std::uint32_t v){regs.write(a,v);}});
            if(!sourceVisible())throw std::runtime_error("CLASS source not visible to User_Ddr reader");
            control.clear();
            pl_source_lease_=sourceLease;
            xunjing::StageTimer preprocess("pl_class_preprocess_and_visibility","pl_class_v2",job.frame);
            control.submit(job);control.wait(std::chrono::milliseconds(2000));
            control.finish([&]{return visibilityAndSample(input_,job.frame,job.buffer);});
            pl_source_lease_.reset();preprocess.Success();
        });
        timer.Success();return result;
    }
private:
    std::vector<float> Execute(const std::function<void()>& prepare) {
        std::unique_lock<std::recursive_mutex> guard(xunjing::NpuExecutionMutex(),std::defer_lock);
        { xunjing::StageTimer timer("zg_mutex_wait","zg330");guard.lock();timer.Success(); }
        if(xunjing::NpuFault().load()) throw std::runtime_error("NPU fault latched; restart required");
        auto dev=device_.cast<icraft::xrt::ZG330Device>();
        const auto saved=dev->mmu_table;
        auto backend=session_->backends[0].cast<icraft::xrt::zg330::ZG330Backend>();
        auto restore=[&]{ if(saved.defined()){dev.updateMMU(saved);dev.clearMMUCache();} };
        try {
            if(backend->map_table.defined()){dev.updateMMU(backend->map_table);dev.clearMMUCache();}
            prepare();
            xunjing::StageTimer execute_timer("zg_execute_readback_reset","zg330");
            auto outputs=session_.forward({input_});
            if(outputs.size()!=1 || !outputs[0].waitForReady(std::chrono::milliseconds(10000)))throw std::runtime_error("ZG classifier output timeout");
            std::ostringstream stream;outputs[0].dump(stream,"SFB");auto bytes=stream.str();
            if(bytes.size()!=16)throw std::runtime_error("ZG classifier expects four logits");
            std::vector<float> result(4);std::memcpy(result.data(),bytes.data(),16);
            xunjing::ResetNpu(device_);restore();execute_timer.Success();return result;
        } catch(...) { xunjing::NpuFault().store(true); throw; }
    }
};
