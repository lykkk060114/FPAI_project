// Optional RGB565 box/center-mark overlay for the existing HDMI pixel stream.
// Coordinates are counted against the final, already-aligned pixel stream.
module video_osd_overlay (
 input wire clk, input wire rst,
 input wire in_vs, input wire in_de,
 input wire [15:0] in_rgb565,
 input wire [521:0] payload,
 output reg [15:0] out_rgb565
);
 reg [11:0] pixel_x, pixel_y;
 reg de_d, vs_d;
 reg hit;
 reg [15:0] hit_color;
 reg [60:0] desc;
 reg [10:0] x0,y0,x1,y1,cx,cy;
 reg valid, center_enable;
 integer i;
 always @(posedge clk or posedge rst) begin
  if (rst) begin pixel_x<=0; pixel_y<=0; de_d<=0; vs_d<=0; end
  else begin
   vs_d<=in_vs;
   if (in_vs && !vs_d) begin pixel_x<=0; pixel_y<=0; de_d<=0; end
   else if (in_de) begin
    if (!de_d) pixel_x<=0;
    else pixel_x<=pixel_x+1'b1;
    de_d<=1;
   end else begin
    pixel_x<=0;
    if (de_d) pixel_y<=pixel_y+1'b1;
    de_d<=0;
   end
  end
 end

 always @* begin
  hit=0; hit_color=in_rgb565; desc=0;
  x0=0;y0=0;x1=0;y1=0;cx=0;cy=0;valid=0;
  center_enable=payload[1];
  if (payload[0] && in_de) begin
   for (i=0;i<8;i=i+1) begin
    desc=payload[34+i*61 +: 61];
    valid=desc[60];
    x0=desc[10:0]; y0=desc[21:11]; x1=desc[32:22]; y1=desc[43:33];
    cx=(x0+x1)>>1; cy=(y0+y1)>>1;
    if (!hit && valid && pixel_x<=12'd1919 && pixel_y<=12'd1079 &&
       (((pixel_x==x0 || pixel_x==x1) && pixel_y>=y0 && pixel_y<=y1) ||
        ((pixel_y==y0 || pixel_y==y1) && pixel_x>=x0 && pixel_x<=x1) ||
        (center_enable && ((pixel_x==cx && pixel_y+12'd4>=cy && pixel_y<=cy+12'd4) ||
                           (pixel_y==cy && pixel_x+12'd4>=cx && pixel_x<=cx+12'd4))))) begin
     hit=1; hit_color=desc[59:44];
    end
   end
  end
  out_rgb565=hit?hit_color:in_rgb565;
 end
endmodule
