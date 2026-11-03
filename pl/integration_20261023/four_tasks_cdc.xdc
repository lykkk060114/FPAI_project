# Bounded CDC buses only; each group has one source and one destination clock.
# Sticky camera error enters only the first synchronizer D. Stage 2 and the
# memory-side state machine retain normal setup/hold timing checks.
set_false_path -to [get_pins U0_ps_ai_peripheral/frame_to_ps_inst0/frame_read_write_m0/frame_fifo_write_m0/wr_wfifo_err_meta_reg/D]
# Sequential-cell filters exclude LUTs whose names share a register prefix.
# Companion ai7030.xdc excludes these destinations from legacy clock false paths.
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/wg_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/ws1_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/wg_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/ws1_reg*}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/rg_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/rs1_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/rg_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/pixels/rs1_reg*}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/request_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/request_capture_reg*}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/response_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_npu_read_port/request_cdc/response_capture_reg*}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/request_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/request_capture_reg*}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/response_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/config_cdc/response_capture_reg*}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/request_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/request_capture_reg*}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/response_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ U0_ps_ai_peripheral/xj_classifier_cdc/response_capture_reg*}]
# The arm payload is constant 1 after arming, but its reset-to-arm transition
# still crosses the domain and is retained by the dedicated capture register.
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/request_hold_reg*}] -to [get_pins -of_objects [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/request_capture_reg*}] -filter {REF_PIN_NAME == D}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/request_hold_reg*}] -to [get_pins -of_objects [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/request_capture_reg*}] -filter {REF_PIN_NAME == D}]
set_max_delay -datapath_only 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/response_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_input_bridge/capture_cdc/response_capture_reg*}]
# P3 OSD frame mailbox.  The payload is a held multi-bit handshake bus; it is
# not a synchronizer and must be bounded for placement/skew rather than timed
# as a phase-related synchronous path.  Its 8 ns datapath bound is below the
# 10 ns earliest two-destination-edge interval (the camera fallback is 5 ns),
# and the payload remains stable until the round-trip response.
set_max_delay -datapath_only 8.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/request_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/request_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/request_capture_reg*}]
set_max_delay -datapath_only 8.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/response_capture_reg*}]
set_bus_skew 5.000 -from [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/response_hold_reg*}] -to [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/response_capture_reg*}]
