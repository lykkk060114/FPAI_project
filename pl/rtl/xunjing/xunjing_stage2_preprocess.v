`timescale 1ns / 1ps
// Xunjing Stage 2. All signals are synchronous to the SDI pixel clock.
// GEOMETRY_ENABLE=0: preserve the reference Demo video geometry exactly.
// GEOMETRY_ENABLE=1: from a FIXED 1920x1080 active raster, select the
//   center 1920x1056 crop (top/bottom 12 pixels), then decimate by 3x3 to
//   640x352. This is CENTER-CROP + NEAREST DECIMATION, NOT letterboxing.
//   IMPORTANT: downstream DMA/software image dimensions MUST be changed
//   before deploying with GEOMETRY_ENABLE=1. No CDC or frame buffering.
// COLOR_MODE: 0 RGB888 pass-through, 1 RGB565 precision in RGB888 bus,
//             2 grayscale in RGB888 bus (approx. BT.601).
module xunjing_stage2_preprocess #(
    parameter integer GEOMETRY_ENABLE = 0,
    parameter integer COLOR_MODE = 0
) (
    input wire clk,
    input wire rst,
    input wire in_vsync,
    input wire in_de,
    input wire [23:0] in_rgb,
    output reg out_vsync,
    output reg out_de,
    output reg [23:0] out_rgb
);
    reg vsync_d;
    reg de_d;
    reg [11:0] x;
    reg [11:0] y;
    wire vsync_rise = in_vsync && !vsync_d;
    wire pixel_selected = (y >= 12 && y < 1068 && x < 1920 && ((y - 12) % 3 == 0) && (x % 3 == 0));
    wire [7:0] r = in_rgb[23:16];
    wire [7:0] g = in_rgb[15:8];
    wire [7:0] b = in_rgb[7:0];
    wire [15:0] gray_sum = r * 8'd77 + g * 8'd150 + b * 8'd29;
    wire [7:0] gray = gray_sum[15:8];
    wire [23:0] colored = (COLOR_MODE == 1) ?
          {r[7:3],3'b000,g[7:2],2'b00,b[7:3],3'b000} :
          (COLOR_MODE == 2) ? {gray,gray,gray} : in_rgb;
    always @(posedge clk) begin
        if (rst) begin
            vsync_d <= 0;
            de_d <= 0;
            x <= 0;
            y <= 0;
            out_vsync <= 0;
            out_de <= 0;
            out_rgb <= 0;
        end else begin
            vsync_d <= in_vsync;
            de_d <= in_de;
            out_vsync <= in_vsync;
            out_de <= in_de && ((GEOMETRY_ENABLE == 0) || pixel_selected);
            out_rgb <= colored;
            if (vsync_rise) begin
                x <= 0;
                y <= 0;
            end else if (in_de) begin
                x <= x + 1'b1;
            end else if (de_d) begin
                x <= 0;
                y <= y + 1'b1;
            end
        end
    end
endmodule
