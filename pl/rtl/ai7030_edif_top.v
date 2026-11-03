//==============================================================================
// Orgnization   : Shanghai Fudan Microelectronics Co., Ltd. Confidential
// File Name     : ai7030_edif_top.v
// Author        : 
// Project       : 
// Create Date   : 
// Description   :
//------------------------------------------------------------------------------
// Modification History :
// Rev     Date         Who          Description
//==============================================================================

module ai7030_edif_top (
  // PS
  inout     [53 : 0]     FIXED_IO_0_mio            ,
  inout     [14 : 0]     DDR_0_addr                , 
  inout     [2  : 0]     DDR_0_ba                  , 
  inout                  DDR_0_cas_n               , 
  inout                  DDR_0_ck_n                , 
  inout                  DDR_0_ck_p                , 
  inout                  DDR_0_cke                 , 
  inout                  DDR_0_cs_n                , 
  inout     [3  : 0]     DDR_0_dm                  , 
  inout     [31 : 0]     DDR_0_dq                  , 
  inout     [3  : 0]     DDR_0_dqs_n               , 
  inout     [3  : 0]     DDR_0_dqs_p               , 
  inout                  DDR_0_odt                 , 
  inout                  DDR_0_ras_n               , 
  inout                  DDR_0_reset_n             , 
  inout                  DDR_0_we_n                , 
  inout                  FIXED_IO_0_ddr_vrn        , 
  inout                  FIXED_IO_0_ddr_vrp        , 
  inout                  FIXED_IO_0_ps_clk         ,
  inout                  FIXED_IO_0_ps_porb        ,
  inout                  FIXED_IO_0_ps_srstb       ,
         
  // PL DDR       
  inout     [ 63 : 0]    ddr3_dq                   ,
  inout     [  7 : 0]    ddr3_dqs_n                ,
  inout     [  7 : 0]    ddr3_dqs_p                ,
  output    [  7 : 0]    ddr3_dm                   ,
  output    [ 14 : 0]    ddr3_addr                 ,
  output    [  2 : 0]    ddr3_ba                   ,
  output                 ddr3_ras_n                ,
  output                 ddr3_cas_n                ,
  output                 ddr3_we_n                 ,
  output                 ddr3_reset_n              ,
  output    [  0 : 0]    ddr3_ck_p                 ,
  output    [  0 : 0]    ddr3_ck_n                 ,
  output    [  0 : 0]    ddr3_cke                  ,
  output    [  0 : 0]    ddr3_cs_n                 ,
  output    [  0 : 0]    ddr3_odt                  ,
       
  input                  sys_rst                   ,
  input                  sys_clk_p                 ,
  input                  sys_clk_n                 ,

  // SDI BUS
  input                  FMC_HPC_DP0_M2C_N         ,
  input                  FMC_HPC_DP0_M2C_P         ,
  // MGT REFCLKs
  input                  FMC_HPC_GBTCLK0_M2C_C_N   ,   //156.25M 
  input                  FMC_HPC_GBTCLK0_M2C_C_P   ,   
  input                  rx_gtx_full_reset         ,  

  output                 HDMI_CLK_P                ,    
  output                 HDMI_CLK_N                , 
  output [2:0]           HDMI_TX_P                 , 
  output [2:0]           HDMI_TX_N                 ,      
 
  output    [  1 : 0]    led                       ,

  input                  PJTAG_TCK                 ,
  input                  PJTAG_TMS                 ,
  input                  PJTAG_TDI                 ,
  output                 PJTAG_TDO                 
);

  // User CustomOp REG BUS
  wire   [ 31 : 0]  User_Reg_awwaddr        ;
  wire   [ 31 : 0]  User_Reg_awwdata        ;
  wire              User_Reg_awwvalid       ;
  wire              User_Reg_awwready       ;
  wire   [ 31 : 0]  User_Reg_araddr         ;
  wire              User_Reg_arvalid        ;
  wire              User_Reg_arready        ;
  wire   [ 31 : 0]  User_Reg_rdata          ;
  wire              User_Reg_rvalid         ;
  wire              User_Reg_rready         ;

  //User CustomOp DATA BUS
  wire   [ 31 : 0]  User_Ddr_awwaddr  ;
  wire   [511 : 0]  User_Ddr_awwdata  ;
  wire   [ 71 : 0]  User_Ddr_awwinfo  ; //{1'b0,pl_awen[63:0],pl_awinfo[5:0],pl_awlast[0]}
  wire              User_Ddr_awwvalid ;
  wire              User_Ddr_awwready ;
  wire   [ 31 : 0]  User_Ddr_araddr   ;
  wire              User_Ddr_arvalid  ;
  wire   [ 31 : 0]  User_Ddr_arinfo   ;  //{25'h0,pl_arinfo,pl_arlast}
  wire              User_Ddr_arready  ;
  wire   [511 : 0]  User_Ddr_rdata    ;
  wire              User_Ddr_rvalid   ;
  wire   [ 31 : 0]  User_Ddr_rinfo    ;  //{25'h0,pl_rinfo,pl_rlast}
  wire              User_Ddr_rready   ;
  wire              gp_clk            ;
  wire              hp_clk            ;
  wire              gp_clk_rst        ;
  wire              hp_clk_rst        ;
  wire              FMC_HPC_DP0_C2M_N ;
  wire              FMC_HPC_DP0_C2M_P ;
  (* KEEP="true" *) wire              led1              ;
  (* KEEP="true" *) wire              led2              ;
  (* KEEP="true" *) wire              led3              ;
  // CAM
  (* KEEP="true" *) wire              cam_hs            ;
  (* KEEP="true" *) wire              cam_vs            ;
  (* KEEP="true" *) wire   [ 23  :0]  cam_rgb           ;
  (* KEEP="true" *) wire              cam_clk           ;
  (* KEEP="true" *) wire              cam_data_en       ;
  // Xunjing Stage2: registered SDI preprocessor. Default is geometry/color bypass.
  localparam integer STAGE2_GEOMETRY_ENABLE = 0;
  localparam integer STAGE2_COLOR_MODE = 0;
  wire [23:0] stage2_rgb;
  wire stage2_vs, stage2_de;
  xunjing_stage2_preprocess #(
      .GEOMETRY_ENABLE(STAGE2_GEOMETRY_ENABLE),
      .COLOR_MODE(STAGE2_COLOR_MODE)
  ) u_xunjing_stage2 (
      .clk(cam_clk), .rst(rx_gtx_full_reset),
      .in_vsync(cam_vs), .in_de(cam_data_en), .in_rgb(cam_rgb),
      .out_vsync(stage2_vs), .out_de(stage2_de), .out_rgb(stage2_rgb)
  );



wire xj_resize_vs,xj_resize_de,xj_camera_reset;
wire [23:0] xj_resize_rgb;
wire [31:0] xj_input_data,xj_input_awaddr,xj_old_awaddr,xj_class_awaddr;
wire [511:0] xj_input_awdata,xj_old_awdata,xj_class_awdata;
wire xj_input_awvalid,xj_input_awready,xj_old_awvalid,xj_old_awready,xj_class_awvalid,xj_class_awready;
wire xj_class_tx_valid,xj_class_tx_ready,xj_class_tx_rsp_valid,xj_class_tx_rsp_ready,xj_class_tx_rsp_error;
wire [31:0] xj_class_tx_addr;wire [511:0] xj_class_tx_data;
wire xj_mem_valid,xj_mem_ready,xj_mem_write,xj_mem_rsp_valid,xj_mem_rsp_ready,xj_mem_rsp_error;
wire [31:0] xj_mem_addr,xj_mem_data;wire [15:0] xj_mem_rsp_data;
wire xj_class_ps_valid,xj_class_ps_ready,xj_class_ps_write,xj_class_ps_rsp_valid,xj_class_ps_rsp_ready;
wire [31:0] xj_class_ps_addr,xj_class_ps_data;
wire xj_post_ps_valid,xj_post_ps_ready,xj_post_ps_rsp_valid,xj_post_ps_rsp_ready;
wire [31:0] xj_post_ps_addr,xj_post_ps_data;
wire xj_gp_path_resetn;
wire [521:0] xj_osd_payload;
wire xj_osd_frame_start;
wire [31:0] xj_osd_applied_frame_id;
xj_reset_release xj_gp_reset_release(.clk(gp_clk),.async_reset(hp_clk_rst),.resetn(xj_gp_path_resetn));
ps_ai_peripheral U0_ps_ai_peripheral(
 .xj_resize_vs(xj_resize_vs),.xj_resize_de(xj_resize_de),.xj_resize_rgb(xj_resize_rgb),.xj_camera_reset(xj_camera_reset),
 .xj_osd_payload(xj_osd_payload),.xj_osd_frame_start(xj_osd_frame_start),
 .xj_gp_path_resetn(xj_gp_path_resetn),
 .xj_mem_valid(xj_mem_valid),.xj_mem_ready(xj_mem_ready),.xj_mem_write(xj_mem_write),
 .xj_mem_addr(xj_mem_addr),.xj_mem_data(xj_mem_data),.xj_mem_rsp_valid(xj_mem_rsp_valid),
 .xj_mem_rsp_ready(xj_mem_rsp_ready),.xj_mem_rsp_data(xj_mem_rsp_data),.xj_mem_rsp_error(xj_mem_rsp_error),
  .FIXED_IO_0_mio         ( FIXED_IO_0_mio      ),
  .DDR_0_addr             ( DDR_0_addr          ), 
  .DDR_0_ba               ( DDR_0_ba            ), 
  .DDR_0_cas_n            ( DDR_0_cas_n         ), 
  .DDR_0_ck_n             ( DDR_0_ck_n          ), 
  .DDR_0_ck_p             ( DDR_0_ck_p          ), 
  .DDR_0_cke              ( DDR_0_cke           ), 
  .DDR_0_cs_n             ( DDR_0_cs_n          ), 
  .DDR_0_dm               ( DDR_0_dm            ), 
  .DDR_0_dq               ( DDR_0_dq            ), 
  .DDR_0_dqs_n            ( DDR_0_dqs_n         ), 
  .DDR_0_dqs_p            ( DDR_0_dqs_p         ), 
  .DDR_0_odt              ( DDR_0_odt           ), 
  .DDR_0_ras_n            ( DDR_0_ras_n         ), 
  .DDR_0_reset_n          ( DDR_0_reset_n       ), 
  .DDR_0_we_n             ( DDR_0_we_n          ), 
  .FIXED_IO_0_ddr_vrn     ( FIXED_IO_0_ddr_vrn  ), 
  .FIXED_IO_0_ddr_vrp     ( FIXED_IO_0_ddr_vrp  ), 
  .FIXED_IO_0_ps_clk      ( FIXED_IO_0_ps_clk   ),
  .FIXED_IO_0_ps_porb     ( FIXED_IO_0_ps_porb  ),
  .FIXED_IO_0_ps_srstb    ( FIXED_IO_0_ps_srstb ),

  .ddr3_dq                ( ddr3_dq            ),
  .ddr3_dqs_n             ( ddr3_dqs_n         ),
  .ddr3_dqs_p             ( ddr3_dqs_p         ),
  .ddr3_dm                ( ddr3_dm            ),
  .ddr3_addr              ( ddr3_addr          ),
  .ddr3_ba                ( ddr3_ba            ),
  .ddr3_ras_n             ( ddr3_ras_n         ),
  .ddr3_cas_n             ( ddr3_cas_n         ),
  .ddr3_we_n              ( ddr3_we_n          ),
  .ddr3_reset_n           ( ddr3_reset_n       ),
  .ddr3_ck_p              ( ddr3_ck_p          ),
  .ddr3_ck_n              ( ddr3_ck_n          ),
  .ddr3_cke               ( ddr3_cke           ),
  .ddr3_cs_n              ( ddr3_cs_n          ),
  .ddr3_odt               ( ddr3_odt           ),
       
  .sys_rst                ( sys_rst            ),
  .sys_clk_p              ( sys_clk_p          ),
  .sys_clk_n              ( sys_clk_n          ),

  .cam_vs                 ( stage2_vs          ),
  .cam_rgb                ( stage2_rgb         ),
  .cam_clk                ( cam_clk            ),
  .cam_data_en            ( stage2_de          ),

  .HDMI_CLK_P             ( HDMI_CLK_P         ),    
  .HDMI_CLK_N             ( HDMI_CLK_N         ), 
  .HDMI_TX_P              ( HDMI_TX_P          ), 
  .HDMI_TX_N              ( HDMI_TX_N          ),      
 
  .led                    ( led                ),  // ouput
  .led3                   ( led3               ),  // input
  .PJTAG_TCK              ( PJTAG_TCK          ),
  .PJTAG_TMS              ( PJTAG_TMS          ),
  .PJTAG_TDI              ( PJTAG_TDI          ),
  .PJTAG_TDO              ( PJTAG_TDO          ),

  .gp_clk                 ( gp_clk             ),
  .hp_clk                 ( hp_clk             ),
  .gp_clk_rst             ( gp_clk_rst         ),
  .hp_clk_rst             ( hp_clk_rst         ),

  .User_Reg_awwaddr       ( User_Reg_awwaddr   ),
  .User_Reg_awwdata       ( User_Reg_awwdata   ),
  .User_Reg_awwvalid      ( User_Reg_awwvalid  ),
  .User_Reg_awwready      ( User_Reg_awwready  ),
  .User_Reg_araddr        ( User_Reg_araddr    ),
  .User_Reg_arvalid       ( User_Reg_arvalid   ),
  .User_Reg_arready       ( User_Reg_arready   ),
  .User_Reg_rdata         ( User_Reg_rdata     ),
  .User_Reg_rvalid        ( User_Reg_rvalid    ),
  .User_Reg_rready        ( User_Reg_rready    ),

  .User_Ddr_awwaddr       ( User_Ddr_awwaddr   ),
  .User_Ddr_awwdata       ( User_Ddr_awwdata   ),
  .User_Ddr_awwinfo       ( User_Ddr_awwinfo   ),  //{1'b0,pl_awen[63:0],pl_awinfo[5:0],pl_awlast[0]}
  .User_Ddr_awwvalid      ( User_Ddr_awwvalid  ),
  .User_Ddr_awwready      ( User_Ddr_awwready  ),
  .User_Ddr_araddr        ( User_Ddr_araddr    ),
  .User_Ddr_arvalid       ( User_Ddr_arvalid   ),
  .User_Ddr_arinfo        ( User_Ddr_arinfo    ),  //{25'h0,pl_arinfo,pl_arlast}
  .User_Ddr_arready       ( User_Ddr_arready   ),
  .User_Ddr_rdata         ( User_Ddr_rdata     ),
  .User_Ddr_rvalid        ( User_Ddr_rvalid    ),
  .User_Ddr_rinfo         ( User_Ddr_rinfo     ),  //{25'h0,pl_rinfo,pl_rlast}
  .User_Ddr_rready        ( User_Ddr_rready    ) 
);


rx_sdi rx_sdi_inst (

    .FMC_HPC_DP0_C2M_N          (FMC_HPC_DP0_C2M_N        ),
    .FMC_HPC_DP0_C2M_P          (FMC_HPC_DP0_C2M_P        ),
    .FMC_HPC_DP0_M2C_N          (FMC_HPC_DP0_M2C_N        ),
    .FMC_HPC_DP0_M2C_P          (FMC_HPC_DP0_M2C_P        ),

    .FMC_HPC_GBTCLK0_M2C_C_N    (FMC_HPC_GBTCLK0_M2C_C_N  ),   //156.25M 
    .FMC_HPC_GBTCLK0_M2C_C_P    (FMC_HPC_GBTCLK0_M2C_C_P  ),

    .sys_clk_200m               (hp_clk                   ),   
    
    .led1                       (led1                     ),
    .led2                       (led2                     ),
    .led3                       (led3                     ),
    .cam_hs                     (cam_hs                   ),
    .cam_vs                     (cam_vs                   ),
    .cam_rgb                    (cam_rgb                  ),
    .cam_clk                    (cam_clk                  ),
    .cam_data_en                (cam_data_en              ),
   
    .rx_gtx_full_reset_n        (~rx_gtx_full_reset       ),
    .rx_gtx_reset_n             (~rx_gtx_full_reset       ) 
);


assign User_Ddr_awwinfo = {1'b0, {64{1'b1}}, 6'b0, 1'b1};
assign User_Ddr_arinfo  = {25'h0, 6'b0, 1'b1};


wire xj_legacy_awready,xj_legacy_awvalid,xj_legacy_arvalid,xj_legacy_arready;
wire xj_legacy_rvalid,xj_legacy_rready;wire [31:0] xj_legacy_rdata,xj_csr_data,xj_class_data,xj_post_data,xj_osd_data;
wire xj_wr_class=User_Reg_awwaddr[17:12]==6'h01;
wire xj_wr_input=User_Reg_awwaddr[17:12]==6'h04;
wire xj_wr_post=User_Reg_awwaddr[17:12]==6'h03;
wire xj_wr_osd=User_Reg_awwaddr[17:12]==6'h02;
reg xj_write_active;reg [1:0] xj_write_owner,xj_write_turn,xj_write_pick;
wire xj_wr_select=xj_wr_class||xj_wr_input||xj_wr_post||xj_wr_osd;
wire xj_rd_select=User_Reg_araddr[17:12]==6'h01||User_Reg_araddr[17:12]==6'h04||User_Reg_araddr[17:12]==6'h03||User_Reg_araddr[17:12]==6'h02;
assign xj_csr_data=User_Reg_araddr[17:12]==6'h03 ? xj_post_data :
                   User_Reg_araddr[17:12]==6'h01 ? xj_class_data :
                   User_Reg_araddr[17:12]==6'h02 ? xj_osd_data : xj_input_data;
reg xj_read_pending,xj_read_owner;reg [31:0] xj_read_data;
assign xj_legacy_awvalid=User_Reg_awwvalid&&!xj_wr_select;
assign User_Reg_awwready=xj_wr_select?!gp_clk_rst:xj_legacy_awready;
assign xj_legacy_arvalid=User_Reg_arvalid&&!xj_rd_select&&!xj_read_pending;
assign User_Reg_arready=!xj_read_pending&&(xj_rd_select?!gp_clk_rst:xj_legacy_arready);
assign User_Reg_rvalid=xj_read_pending&&(xj_read_owner?1'b1:xj_legacy_rvalid);
assign User_Reg_rdata=xj_read_owner?xj_read_data:xj_legacy_rdata;
assign xj_legacy_rready=User_Reg_rready&&xj_read_pending&&!xj_read_owner;
always @(posedge gp_clk)begin
 if(gp_clk_rst)begin xj_read_pending<=0;xj_read_owner<=0;xj_read_data<=0;end
 else begin
  if(User_Reg_arvalid&&User_Reg_arready)begin
   xj_read_pending<=1;xj_read_owner<=xj_rd_select;xj_read_data<=xj_csr_data;
  end
  if(User_Reg_rvalid&&User_Reg_rready)xj_read_pending<=0;
 end
end
xj_classifier_control xj_classifier(
 .clk(gp_clk),.resetn(xj_gp_path_resetn),
 .wr_valid(User_Reg_awwvalid&&User_Reg_awwready&&xj_wr_class),
 .wr_addr(User_Reg_awwaddr[11:0]),.wr_data(User_Reg_awwdata),
 .rd_addr(User_Reg_araddr[11:0]),.rd_data(xj_class_data),
 .mem_valid(xj_class_ps_valid),.mem_ready(xj_class_ps_ready),.mem_write(xj_class_ps_write),.mem_addr(xj_class_ps_addr),.mem_data(xj_class_ps_data),
 .mem_rsp_valid(xj_class_ps_rsp_valid),.mem_rsp_ready(xj_class_ps_rsp_ready),.mem_rsp_data(xj_mem_rsp_data),.mem_rsp_error(xj_mem_rsp_error),
 .ddr_awvalid(xj_class_tx_valid),.ddr_awready(xj_class_tx_ready),.ddr_awaddr(xj_class_tx_addr),.ddr_awdata(xj_class_tx_data),
 .ddr_rsp_valid(xj_class_tx_rsp_valid),.ddr_rsp_error(xj_class_tx_rsp_error),.ddr_rsp_ready(xj_class_tx_rsp_ready));

wire class_cdc_m_valid,class_cdc_m_ready,class_cdc_m_rsp_ready,class_cdc_m_rsp_valid;
wire [544:0] class_cdc_m_payload;wire class_cdc_s_rsp_valid;wire [0:0] class_cdc_s_rsp_data;
reg class_hp_pending;reg [31:0] class_hp_addr;reg [511:0] class_hp_data;
xj_command_cdc #(.REQUEST_BITS(544),.RESPONSE_BITS(1)) xj_class_write_cdc(
 .s_clk(gp_clk),.s_resetn(xj_gp_path_resetn),.s_valid(xj_class_tx_valid),.s_ready(xj_class_tx_ready),
 .s_payload({xj_class_tx_addr,xj_class_tx_data}),
 .s_rsp_valid(class_cdc_s_rsp_valid),.s_rsp_ready(xj_class_tx_rsp_ready),.s_response(class_cdc_s_rsp_data),
 .m_clk(hp_clk),.m_resetn(~hp_clk_rst),.m_valid(class_cdc_m_valid),.m_ready(class_cdc_m_ready),.m_payload(class_cdc_m_payload),
 .m_rsp_valid(class_cdc_m_rsp_valid),.m_rsp_ready(class_cdc_m_rsp_ready),.m_response(1'b0));
assign xj_class_tx_rsp_valid=class_cdc_s_rsp_valid;
assign xj_class_tx_rsp_error=class_cdc_s_rsp_data[0];
assign class_cdc_m_ready=!class_hp_pending;
assign xj_class_awaddr=class_hp_addr;
assign xj_class_awdata=class_hp_data;
assign xj_class_awvalid=class_hp_pending;
assign class_cdc_m_rsp_valid=xj_write_active&&xj_write_owner==2'd2&&User_Ddr_awwready;
always @(posedge hp_clk)begin
 if(hp_clk_rst)begin class_hp_pending<=0;class_hp_addr<=0;class_hp_data<=0;end
 else begin
  if(class_cdc_m_valid&&class_cdc_m_ready)begin
   class_hp_pending<=1;class_hp_addr<=class_cdc_m_payload[543:512];class_hp_data<=class_cdc_m_payload[511:0];
  end
  if(class_hp_pending&&xj_class_awready)class_hp_pending<=0;
 end
end

wire [31:0] xj_legacy_ddr_araddr,xj_post_addr,xj_post_rinfo;
wire [511:0] xj_legacy_ddr_rdata,xj_post_rdata;
wire xj_legacy_ddr_arvalid,xj_legacy_ddr_arready,xj_legacy_ddr_rvalid,xj_legacy_ddr_rready;
wire xj_post_valid,xj_post_ready,xj_post_rsp_valid,xj_post_rsp_ready,xj_post_rsp_error;
xj_customop_read_path xj_npu_read_port(
 .gp_clk(gp_clk),.hp_clk(hp_clk),.resetn(~hp_clk_rst),.gp_resetn(xj_gp_path_resetn),
 .req_valid(xj_post_valid),.req_ready(xj_post_ready),.req_addr(xj_post_addr),
 .rsp_valid(xj_post_rsp_valid),.rsp_ready(xj_post_rsp_ready),.rsp_data(xj_post_rdata),.rsp_info(xj_post_rinfo),.rsp_error(xj_post_rsp_error),
 .legacy_araddr(xj_legacy_ddr_araddr),.legacy_arvalid(xj_legacy_ddr_arvalid),.legacy_arready(xj_legacy_ddr_arready),
 .legacy_rdata(xj_legacy_ddr_rdata),.legacy_rvalid(xj_legacy_ddr_rvalid),.legacy_rready(xj_legacy_ddr_rready),
 .araddr(User_Ddr_araddr),.arvalid(User_Ddr_arvalid),.arready(User_Ddr_arready),
 .rdata(User_Ddr_rdata),.rinfo(User_Ddr_rinfo),.rvalid(User_Ddr_rvalid),.rready(User_Ddr_rready));
xj_postprocess_control xj_postprocess(
 .clk(gp_clk),.resetn(xj_gp_path_resetn),
 .wr_valid(User_Reg_awwvalid&&User_Reg_awwready&&xj_wr_post),.wr_addr(User_Reg_awwaddr[11:0]),.wr_data(User_Reg_awwdata),
 .rd_addr(User_Reg_araddr[11:0]),.rd_data(xj_post_data),
 .mem_valid(xj_post_valid),.mem_ready(xj_post_ready),.mem_addr(xj_post_addr),
 .mem_rsp_valid(xj_post_rsp_valid),.mem_rsp_ready(xj_post_rsp_ready),.mem_rsp_data(xj_post_rdata),
 .mem_rsp_info(xj_post_rinfo),.mem_rsp_error(xj_post_rsp_error),
 .ps_valid(xj_post_ps_valid),.ps_ready(xj_post_ps_ready),.ps_addr(xj_post_ps_addr),.ps_data(xj_post_ps_data),
 .ps_rsp_valid(xj_post_ps_rsp_valid),.ps_rsp_ready(xj_post_ps_rsp_ready),.ps_rsp_error(xj_mem_rsp_error));

xj_post_ps_arbiter xj_post_class_ps_port(
 .clk(gp_clk),.resetn(xj_gp_path_resetn),
 .class_valid(xj_class_ps_valid),.class_ready(xj_class_ps_ready),.class_write(xj_class_ps_write),
 .class_addr(xj_class_ps_addr),.class_data(xj_class_ps_data),
 .class_rsp_valid(xj_class_ps_rsp_valid),.class_rsp_ready(xj_class_ps_rsp_ready),
 .post_valid(xj_post_ps_valid),.post_ready(xj_post_ps_ready),.post_addr(xj_post_ps_addr),.post_data(xj_post_ps_data),
 .post_rsp_valid(xj_post_ps_rsp_valid),.post_rsp_ready(xj_post_ps_rsp_ready),
 .mem_valid(xj_mem_valid),.mem_ready(xj_mem_ready),.mem_write(xj_mem_write),.mem_addr(xj_mem_addr),.mem_data(xj_mem_data),
 .mem_rsp_valid(xj_mem_rsp_valid),.mem_rsp_ready(xj_mem_rsp_ready));

xj_osd_control xj_osd_control_inst(
 .gp_clk(gp_clk),.gp_resetn(xj_gp_path_resetn),
 .wr_valid(User_Reg_awwvalid&&User_Reg_awwready&&xj_wr_osd),
 .wr_addr(User_Reg_awwaddr[11:0]),.wr_data(User_Reg_awwdata),
 .rd_addr(User_Reg_araddr[11:0]),.rd_data(xj_osd_data),
 .video_clk(cam_clk),.video_resetn(!xj_camera_reset),.frame_start(xj_osd_frame_start),
 .active_payload(xj_osd_payload),.applied_frame_id(xj_osd_applied_frame_id));

adder_top U_adder_top(
  .itf_ra_awaddr    (User_Reg_awwaddr      ),
  .itf_ra_awdata    (User_Reg_awwdata      ),
  .itf_ra_awvalid   (xj_legacy_awvalid),
  .itf_ra_awready   (xj_legacy_awready),
  .itf_ra_araddr    (User_Reg_araddr       ),
  .itf_ra_arvalid   (xj_legacy_arvalid),
  .itf_ra_arready   (xj_legacy_arready),
  .itf_ra_rdata     (xj_legacy_rdata),
  .itf_ra_rvalid    (xj_legacy_rvalid),
  .itf_ra_rready    (xj_legacy_rready),
   
  .itf_awaddr       (xj_old_awaddr         ),
  .itf_awdata       (xj_old_awdata         ),
  .itf_awvalid      (xj_old_awvalid        ),
  .itf_awready      (xj_old_awready        ),
  .itf_araddr       (xj_legacy_ddr_araddr),
  .itf_arvalid      (xj_legacy_ddr_arvalid),
  .itf_arready      (xj_legacy_ddr_arready),
  .itf_rdata        (xj_legacy_ddr_rdata),
  .itf_rvalid       (xj_legacy_ddr_rvalid),
  .itf_rready       (xj_legacy_ddr_rready),

  .reset_reg        (                      ),
  .clk              (hp_clk                ),
  .ra_clk           (gp_clk                ),
  .rst_n            (~hp_clk_rst           ),
  .ra_rst_n         (~gp_clk_rst           )
);


xj_input_control xj_input_bridge(
 .gp_clk(gp_clk),.gp_resetn(xj_gp_path_resetn),.hp_clk(hp_clk),.hp_resetn(~hp_clk_rst),
 .cam_clk(cam_clk),.cam_reset(xj_camera_reset),.vs(xj_resize_vs),.de(xj_resize_de),.rgb(xj_resize_rgb),
 .wr_valid(User_Reg_awwvalid&&User_Reg_awwready&&xj_wr_input),.wr_addr(User_Reg_awwaddr[11:0]),.wr_data(User_Reg_awwdata),
 .rd_addr(User_Reg_araddr[11:0]),.rd_data(xj_input_data),
 .awaddr(xj_input_awaddr),.awdata(xj_input_awdata),.awvalid(xj_input_awvalid),.awready(xj_input_awready));
// Lock each combined beat before exposing it; payload remains stable until ready.
always @*begin
 xj_write_pick=xj_write_turn;
 case(xj_write_turn)
  0:if(xj_old_awvalid)xj_write_pick=0;else if(xj_input_awvalid)xj_write_pick=1;else if(xj_class_awvalid)xj_write_pick=2;
  1:if(xj_input_awvalid)xj_write_pick=1;else if(xj_class_awvalid)xj_write_pick=2;else if(xj_old_awvalid)xj_write_pick=0;
  default:if(xj_class_awvalid)xj_write_pick=2;else if(xj_old_awvalid)xj_write_pick=0;else if(xj_input_awvalid)xj_write_pick=1;
 endcase
end
always @(posedge hp_clk)begin
 if(hp_clk_rst)begin xj_write_active<=0;xj_write_owner<=0;xj_write_turn<=0;end
 else if(!xj_write_active)begin
  if(xj_old_awvalid||xj_input_awvalid||xj_class_awvalid)begin
   xj_write_active<=1;xj_write_owner<=xj_write_pick;
  end
 end else if(User_Ddr_awwvalid&&User_Ddr_awwready)begin
  xj_write_active<=0;xj_write_turn<=xj_write_owner==2?0:xj_write_owner+1'b1;
 end
end
assign User_Ddr_awwaddr=xj_write_owner==0?xj_old_awaddr:xj_write_owner==1?xj_input_awaddr:xj_class_awaddr;
assign User_Ddr_awwdata=xj_write_owner==0?xj_old_awdata:xj_write_owner==1?xj_input_awdata:xj_class_awdata;
assign User_Ddr_awwvalid=xj_write_active&&(xj_write_owner==0?xj_old_awvalid:xj_write_owner==1?xj_input_awvalid:xj_class_awvalid);
assign xj_old_awready=xj_write_active&&xj_write_owner==0&&User_Ddr_awwready;
assign xj_input_awready=xj_write_active&&xj_write_owner==1&&User_Ddr_awwready;
assign xj_class_awready=xj_write_active&&xj_write_owner==2&&User_Ddr_awwready;
endmodule
