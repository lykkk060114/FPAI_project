`timescale 1ns/1ps
module tb_frame_fifo_error_cdc;
 reg mem_clk=0, camera_clk=0;
 always #2.5 mem_clk=~mem_clk;
 always #3.367 camera_clk=~camera_clk;
 reg rst=1, write_req=0, err=0, burst_finish=0;
 reg [15:0] used=0;
 wire ack, done, burst_req, fifo_clear;
 wire [7:0] burst_len;
 wire [15:0] burst_addr;
 frame_fifo_write #(.ADDR_BITS(16),.BUSRT_BITS(8),.BURST_SIZE(16)) dut (
  .rst(rst),.mem_clk(mem_clk),.wr_burst_req(burst_req),
  .wr_burst_len(burst_len),.wr_burst_addr(burst_addr),
  .wr_burst_data_req(1'b0),.wr_burst_finish(burst_finish),
  .write_req(write_req),.write_req_ack(ack),.write_finish(done),
  .write_addr_0(16'h1000),.write_addr_1(16'h2000),
  .write_addr_2(16'h3000),.write_addr_3(16'h4000),
  .write_addr_index(2'b0),.write_len(16'd32),.fifo_aclr(fifo_clear),
  .wr_wfifo_err(err),.rdusedw(used)
 );
 task cycles(input integer count);
  repeat(count) begin @(posedge mem_clk); #0.1; end
 endtask
 task start_frame;
  @(negedge mem_clk); write_req=1;
  wait(ack); cycles(4);
  if(!fifo_clear) $fatal(1,"Missing frame FIFO clear during acknowledge");
  @(negedge mem_clk); write_req=0;
  wait(!ack); cycles(1);
 endtask
 task finish_burst;
  @(negedge mem_clk); burst_finish=1;
  cycles(1);
  @(negedge mem_clk); burst_finish=0;
 endtask
 task await_done;
  integer n;
  begin
   n=0;
   while(!done && n<12) begin cycles(1); n=n+1; end
   if(!done) $fatal(1,"Frame did not finish after sticky error/burst completion");
   if(burst_req) $fatal(1,"Burst still requested at frame completion");
   cycles(2);
  end
 endtask
 task camera_error(input bit value);
  @(negedge camera_clk); err=value;
 endtask
 integer i;
 initial begin
  cycles(4); @(negedge mem_clk); rst=0;
  // Normal frame: exactly two bursts, preserving addresses and lengths.
  start_frame();
  for(i=0;i<2;i=i+1) begin
   @(negedge mem_clk); used=16;
   wait(burst_req); #0.1;
   if(burst_len!=16 || burst_addr!=16'h1000+i*16)
    $fatal(1,"Normal burst address/length changed");
   @(negedge mem_clk); used=0;
   finish_burst();
  end
  await_done();
  // Error with no queued data must finish without issuing a burst.
  start_frame(); camera_error(1);
  await_done();
  // Re-arm after clearing the camera's sticky error.
  camera_error(0); start_frame();
  cycles(4);
  if(done || burst_req) $fatal(1,"Stale error aborted a re-armed frame");
  // Error during a live burst must not cancel that memory transaction.
  @(negedge mem_clk); used=16;
  wait(burst_req);
  @(negedge mem_clk); used=0;
  camera_error(1); cycles(6);
  if(!burst_req || done) $fatal(1,"Error cancelled an outstanding burst");
  finish_burst(); await_done();
  // Reset at an arbitrary camera phase, then restart and complete normally.
  #1.1; rst=1; #0.1;
  if(burst_req || done) $fatal(1,"Reset failed to clear frame control");
  camera_error(0); cycles(3);
  @(negedge mem_clk); rst=0;
  start_frame();
  for(i=0;i<2;i=i+1) begin
   @(negedge mem_clk); used=16;
   wait(burst_req);
   @(negedge mem_clk); used=0;
   finish_burst();
  end
  await_done();
  $display("FRAME_FIFO_ERROR_CDC_PASS normal/error/drain/rearm/reset");
  $finish;
 end
 initial begin #20000; $fatal(1,"Watchdog timeout"); end
endmodule
