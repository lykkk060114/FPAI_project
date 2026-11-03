# Both endpoints use the SAME muxed cam_clk net. The two clocks cannot be
# selected simultaneously on that net. Cut ONLY cross-mode recovery/removal
# from the synchronized release Q to the protected first-stage async CLR pins.
# Preserve same-mode recovery/removal and every Gray/payload D-pin bound.
# Do not group the master clocks or exempt asynchronous reset paths globally.
set_false_path -from [get_clocks clk_pll_i] -to [get_clocks rx_sdi_inst/SDI0/SDI/GTX_i/gtxe2_i/RXOUTCLK] -through [get_pins -hier -filter {NAME == xj_input_bridge/camera_path_reset/release_pipe_reg[1]/Q}] -through [get_pins -of_objects [get_cells -hier -filter {IS_SEQUENTIAL == 1 && (NAME =~ xj_input_bridge/pixels/rs1_reg* || NAME =~ xj_input_bridge/capture_cdc/request_capture_reg*)}] -filter {REF_PIN_NAME == CLR}]
set_false_path -from [get_clocks rx_sdi_inst/SDI0/SDI/GTX_i/gtxe2_i/RXOUTCLK] -to [get_clocks clk_pll_i] -through [get_pins -hier -filter {NAME == xj_input_bridge/camera_path_reset/release_pipe_reg[1]/Q}] -through [get_pins -of_objects [get_cells -hier -filter {IS_SEQUENTIAL == 1 && (NAME =~ xj_input_bridge/pixels/rs1_reg* || NAME =~ xj_input_bridge/capture_cdc/request_capture_reg*)}] -filter {REF_PIN_NAME == CLR}]

# The P3 OSD mailbox uses the existing video/camera reset fanout directly.
# cam_clk is a muxed net: only one of clk_pll_i (fallback) and RXOUTCLK (SDI)
# is active at a time.  Cut only the impossible cross-mode recovery checks;
# same-mode recovery and all payload D-pin timing remain constrained.
set_false_path -from [get_clocks clk_pll_i] -to [get_clocks rx_sdi_inst/SDI0/SDI/GTX_i/gtxe2_i/RXOUTCLK] -through [get_pins -hier -filter {NAME =~ */a2sync_reset_cam0_clk/reset_2d_reg*/Q}] -through [get_pins -of_objects [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/request_capture_reg*}] -filter {REF_PIN_NAME == CLR}]
set_false_path -from [get_clocks rx_sdi_inst/SDI0/SDI/GTX_i/gtxe2_i/RXOUTCLK] -to [get_clocks clk_pll_i] -through [get_pins -hier -filter {NAME =~ */a2sync_reset_cam0_clk/reset_2d_reg*/Q}] -through [get_pins -of_objects [get_cells -hier -filter {IS_SEQUENTIAL == 1 && NAME =~ xj_osd_control_inst/osd_frame_mailbox/request_capture_reg*}] -filter {REF_PIN_NAME == CLR}]
