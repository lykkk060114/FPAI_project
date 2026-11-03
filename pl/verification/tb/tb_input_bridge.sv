`timescale 1ns/1ps
module tb_input_bridge;
 reg gp=0,hp=0,cam=0,rst=0,vs=0,de=0;reg [23:0] rgb=0;
 always #5 gp=~gp;always #2.5 hp=~hp;always #6.7 cam=~cam;
 reg wv=0;reg [11:0] wa=0,ra=0;reg [31:0] wd=0;wire [31:0] rd,aa;wire [511:0] ad;wire av;reg ar=0;
 integer cycle=0,count=0,p,k,channel,value,phase=0;reg [31:0] expected;reg [511:0] held;reg [31:0] held_addr;reg stalled=0;
 xj_input_control dut(.gp_clk(gp),.gp_resetn(rst),.hp_clk(hp),.hp_resetn(rst),.cam_clk(cam),.cam_reset(1'b0),
 .vs(vs),.de(de),.rgb(rgb),.wr_valid(wv),.wr_addr(wa),.wr_data(wd),.rd_addr(ra),.rd_data(rd),
 .awaddr(aa),.awdata(ad),.awvalid(av),.awready(ar));
 function [31:0] fp(input integer x);
  integer h,j;reg [31:0] f;reg [7:0] e;
  begin h=0;for(j=0;j<8;j=j+1)if((x>>j)&1)h=j;
   f=x<<(23-h);e=127+h;fp=x==0?0:{1'b0,e,f[22:0]};end
 endfunction
 always @(negedge hp)begin cycle=cycle+1;ar=phase==2?0:(cycle%7!=0&&cycle%7!=1);end
 always @(posedge hp)if(rst)begin
  if(stalled&&(!av||ad!==held||aa!==held_addr))$fatal(1,"write changed under stall");
  stalled<=av&&!ar;held<=ad;held_addr<=aa;
  if(av&&ar)begin
   if(aa!==32'h100000+count*64)$fatal(1,"bad address %h count %d",aa,count);
   if(phase==0)for(k=0;k<16;k=k+1)begin
    p=(count*16+k)/3;channel=(count*16+k)%3;
    case(channel)0:value=(p+2)&255;1:value=(p+1)&255;2:value=p&255;endcase
    expected=fp(value);
    if(ad[k*32+:32]!==expected)$fatal(1,"bad data beat %d lane %d",count,k);
   end
   count=count+1;
  end
 end
 task wr(input [11:0] a,input [31:0] d);
  begin @(negedge gp);wa=a;wd=d;wv=1;@(negedge gp);wv=0;end
 endtask
 task configure;
  begin wr('hc,'h100000);wr('h10,2703360);wr('h14,123);wr('h18,2);wr('h1c,10000000);wr('h20,16);wr(8,1);end
 endtask
 task camera_frame(input integer n);
  integer j;begin
   repeat(40)@(negedge cam);vs=1;@(negedge cam);vs=0;
   for(j=0;j<n;j=j+1)begin
    @(negedge cam);de=1;rgb={8'(j),8'(j+1),8'(j+2)};
   end
   @(negedge cam);de=0;
  end
 endtask
 task wait_status(input integer bitno);
  integer t;begin ra=4;t=0;
   while(!rd[bitno]&&t<12000000)begin @(negedge gp);t=t+1;end
   if(t==12000000)$fatal(1,"timeout status=%h state=%d",rd,dut.state);
  end
 endtask
 initial begin
  repeat(12)@(negedge gp);rst=1;repeat(12)@(negedge gp);
  ra=4;#1;if(!rd[4]||rd[0])$fatal(1,"INPUT READY missing after reset: %h",rd);
  configure();camera_frame(225280);wait_status(5);
  if(rd[4]||!rd[0])$fatal(1,"INPUT READY must clear while busy: %h",rd);
  if(count!=42240||rd[1])$fatal(1,"acceptance count or premature done");
  ra='h30;#1;if(rd!==1)$fatal(1,"VS start diagnostic mismatch: %h",rd);
  ra='h34;#1;if(rd!==1)$fatal(1,"capture end diagnostic mismatch: %h",rd);
  ra='h38;#1;if(rd!==225280)$fatal(1,"last DE diagnostic mismatch: %h",rd);
  ra='h3c;#1;if(rd!==0)$fatal(1,"FIFO full diagnostic mismatch: %h",rd);
  ra='h40;#1;if(!rd[1]||rd[0])$fatal(1,"camera reset diagnostic mismatch: %h",rd);
  ra=4;
  wr(8,8);wait_status(1);if(rd[2])$fatal(1,"frame error %h",rd);
  if(!rd[4]||rd[0])$fatal(1,"INPUT READY not restored after done: %h",rd);
  $display("PASS full frame 225280 pixels / 42240 beats / BGR FP32 + stalls + commit");
  phase=1;count=0;configure();camera_frame(100);wr(8,4);wait_status(1);
  if(!rd[2])$fatal(1,"abort not reported");
  $display("PASS abort drains and reports invalid frame");
  phase=2;count=0;configure();
  fork camera_frame(225280);begin repeat(800000)@(negedge hp);phase=1;end join
  wait_status(1);if(!rd[2])$fatal(1,"overflow not reported");
  $display("PASS overflow invalidates frame and drains held write");
  phase=0;count=0;configure();camera_frame(225280);wait_status(5);wr(8,8);wait_status(1);
  if(rd[2]||count!=42240)$fatal(1,"rearm failed %h count %d",rd,count);
  $display("PASS rearm after overflow; ALL INPUT BRIDGE TESTS PASSED");$finish;
 end
 initial begin #300000000;$fatal(1,"global timeout");end
endmodule
