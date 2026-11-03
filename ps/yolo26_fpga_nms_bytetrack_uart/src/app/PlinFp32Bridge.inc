// ImageMake emits signed 8-bit BGR; this model's first HardOp expects
// unnormalized FP32 BGR (the compiled graph performs channel swap/scale).
#if defined(USE_ZG330_BACKEND)
                        auto &bridge_tensor = msg.image_tensors.at(0);
                        if (!bridge_tensor.dtype()->element_dtype.getStorageType().isFP32())
                            throw std::runtime_error("PLIN FP32 bridge requires FP32 network input");
                        if (xunjing::PlDirectEnabled()) {
                            pl_direct->Complete(bridge_tensor, buffer_index);
                        } else {
                        const std::size_t bridge_elements =
                            camera_->getNetWidth() * camera_->getNetHeight() * 3;
                        std::size_t bridge_shape_elements = 1;
                        for (auto dimension : bridge_tensor.dtype()->shape)
                            bridge_shape_elements *= static_cast<std::size_t>(dimension);
                        if (bridge_shape_elements != bridge_elements)
                            throw std::runtime_error("PLIN bridge input shape mismatch");
                        const auto bridge_start = std::chrono::steady_clock::now();
                        const double bridge_cpu_start = xunjing::threadCpuMs();
                        thread_local std::vector<int8_t> bridge_bytes;
                        thread_local std::vector<float> bridge_values;
                        bridge_bytes.resize(bridge_elements);
                        bridge_values.resize(bridge_elements);
                        this->device_.defaultMemRegion().read(
                            reinterpret_cast<char *>(bridge_bytes.data()),
                            icraft::xrt::MemPtr(imagemake_wddr_base_groups_[buffer_index]), bridge_elements);
                        const auto bridge_read_end = std::chrono::steady_clock::now();
                        xunjing::SignedBgrToFp32(bridge_bytes.data(), bridge_values.data(), bridge_elements);
                        const auto bridge_convert_end = std::chrono::steady_clock::now();
                        bridge_tensor.write(0, reinterpret_cast<char *>(bridge_values.data()),
                                            bridge_elements * sizeof(float));
                        if (const char* mapping_root = std::getenv("XUNJING_LINK_MAPPING_DIR"); mapping_root && *mapping_root) {
                            try {
                                auto backend = imk_or_icore_sessions_[buffer_index]->backends[0].cast<BackendType>();
                                auto forwards = imk_or_icore_sessions_[buffer_index].getForwards();
                                auto input_op = std::get<0>(forwards.at(0));
                                if(input_op->inputs.size()==0) throw std::runtime_error("input op has no inputs");
                                xunjing::link001::CaptureMapping(backend,input_op->inputs[0]->v_id,buffer_index,msg.meta.source_id);
                            } catch(const std::exception& e) { std::cerr<<"[LINK-001 mapping metadata] "<<e.what()<<'\n'; }
                        }
                        // Optional LINK-001 read-only evidence; include Link001InputEvidence.hpp once at header scope.
                        if (const char* link_root = std::getenv("XUNJING_LINK_INPUT_REFERENCE_DIR"); link_root && *link_root) {
                            try {
                                auto link_forwards = imk_or_icore_sessions_[buffer_index].getForwards();
                                auto link_forward_info = imk_or_icore_sessions_[buffer_index]->backends[0].cast<BackendType>()->forward_info;
                                auto link_input_op = std::get<0>(link_forwards.at(0));
                                if (link_input_op->inputs.size() == 0) throw std::runtime_error("LINK input op has no inputs");
                                auto link_vid = link_input_op->inputs[0]->v_id;
                                auto link_info = link_forward_info->value_map.at(link_vid);
                                xunjing::link001::Capture(bridge_tensor, bridge_values, link_vid,
                                    link_info->phy_addr, link_info->byte_size,
                                    imagemake_wddr_base_groups_[buffer_index], buffer_index,
                                    msg.meta.source_id, std::to_string(msg.meta.timestamp), this->device_);
                            } catch (const std::exception& e) {
                                std::cerr << "[LINK-001 metadata] " << e.what() << '\n';
                            }
                        }
                        const auto bridge_end = std::chrono::steady_clock::now();
                        thread_local xunjing::FrameTimingCsv bridge_timing("plin_bridge", "read_wall_ms,convert_wall_ms,unused_track_ms,unused_classify_ms,unused_draw_ms,write_wall_ms,bridge_wall_ms,bridge_thread_cpu_ms");
                        bridge_timing.Write(msg.meta.source_id, msg.meta.timestamp, 0, 0, false,
                            xunjing::elapsedMs(bridge_start,bridge_read_end),
                            xunjing::elapsedMs(bridge_read_end,bridge_convert_end), 0, 0, 0,
                            xunjing::elapsedMs(bridge_convert_end,bridge_end),
                            xunjing::elapsedMs(bridge_start,bridge_end),xunjing::threadCpuMs()-bridge_cpu_start);
                        static bool bridge_logged = false;
                        if (!bridge_logged) {
                            spdlog::info("PLIN bridge: signed8 BGR -> FP32 BGR 0..255, elements={}", bridge_elements);
                            bridge_logged = true;
                        }
                        }
#endif
