`timescale 1ns/1ps
module p3_osd_tb;
  reg gp_clk=0, video_clk=0;
  always #5 gp_clk=~gp_clk;
  always #7 video_clk=~video_clk;
  reg gp_resetn=0, video_resetn=0;
  reg wr_valid=0;
  reg [11:0] wr_addr=0, rd_addr=0;
  reg [31:0] wr_data=0;
  wire [31:0] rd_data, applied_frame_id;
  wire [521:0] payload;
  reg frame_start=0;
  wire [15:0] out_rgb;
  reg in_vs=0, in_de=0;
  reg [15:0] in_rgb=16'h1234;
  integer x,y;

  xj_osd_control dut (
    .gp_clk(gp_clk), .gp_resetn(gp_resetn),
    .wr_valid(wr_valid), .wr_addr(wr_addr), .wr_data(wr_data),
    .rd_addr(rd_addr), .rd_data(rd_data),
    .video_clk(video_clk), .video_resetn(video_resetn),
    .frame_start(frame_start), .active_payload(payload),
    .applied_frame_id(applied_frame_id));

  video_osd_overlay overlay (
    .clk(video_clk), .rst(!video_resetn), .in_vs(in_vs), .in_de(in_de),
    .in_rgb565(in_rgb), .payload(payload), .out_rgb565(out_rgb));

  task write_reg(input [11:0] a, input [31:0] d);
    begin
      @(negedge gp_clk); wr_addr=a; wr_data=d; wr_valid=1;
      @(negedge gp_clk); wr_valid=0;
    end
  endtask

  initial begin
    repeat (4) @(negedge gp_clk);
    gp_resetn=1; video_resetn=1;
    write_reg(12'h040, (11'd4 << 16) | 11'd10);
    write_reg(12'h044, (11'd8 << 16) | 11'd20);
    write_reg(12'h048, 32'h0001F800);
    write_reg(12'h00C, 32'h00000003); // enable + center mark
    write_reg(12'h010, 32'd77);
    write_reg(12'h008, 32'd1); // commit
    repeat (10) @(negedge video_clk);
    if (payload[0] !== 1'b0) $fatal(1, "payload activated without frame boundary");
    @(negedge video_clk); frame_start=1;
    @(negedge video_clk); frame_start=0;
    repeat (2) @(negedge video_clk);
    if (payload[0] !== 1'b1 || payload[1] !== 1'b1) $fatal(1, "control payload mismatch");
    if (applied_frame_id !== 32'd77) $fatal(1, "frame id mismatch: %h", applied_frame_id);

    // Drive a compact 24x10 raster. Border and center cross are red; interior is passthrough.
    in_vs=1; @(negedge video_clk); in_vs=0;
    for (y=0; y<10; y=y+1) begin
      for (x=0; x<24; x=x+1) begin
        @(negedge video_clk); in_de=1; in_rgb=16'h1234;
        @(posedge video_clk); #1;
        if ((y==4 && x>=10 && x<=20) || (y==8 && x>=10 && x<=20) ||
            ((x==10 || x==20) && y>=4 && y<=8) ||
            (x==15 && y>=2 && y<=10) || (y==6 && x>=11 && x<=19)) begin
          if (out_rgb !== 16'hF800) $fatal(1, "expected red at (%0d,%0d), got %h", x,y,out_rgb);
        end else if (out_rgb !== 16'h1234) $fatal(1, "unexpected overlay at (%0d,%0d): %h", x,y,out_rgb);
      end
      @(negedge video_clk); in_de=0;
      @(posedge video_clk);
    end

    // Disable is frame-atomic, then the stream must pass through unchanged.
    write_reg(12'h00C, 32'd0);
    write_reg(12'h010, 32'd78);
    write_reg(12'h008, 32'd1);
    repeat (10) @(negedge video_clk);
    @(negedge video_clk); frame_start=1;
    @(negedge video_clk); frame_start=0;
    repeat (2) @(negedge video_clk);
    in_de=1; in_rgb=16'hA55A; #1;
    if (out_rgb !== 16'hA55A) $fatal(1, "disabled overlay is not bypassed");
    $display("P3_OSD_PASS register_cdc frame_commit rgb565_box center_bypass");
    $finish;
  end
endmodule
