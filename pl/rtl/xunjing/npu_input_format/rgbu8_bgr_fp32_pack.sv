`timescale 1ns / 1ps
// Logical formatter ONLY. No native addresses, info6, DMA, or visibility fence.
// A successful frame has 225280 RGB pixels and 42240 512bit words. Lane 0
// is the earliest FP32 word; serialize each lane little-endian to memory.
// All emitted words of an aborted/malformed frame must be discarded by caller.
// Input can pause; a non-backpressurable camera needs a separate frame/FIFO owner.
module rgbu8_bgr_fp32_pack (
    input wire clk,
    resetn,
    start,
    abort,
    input wire [31:0] start_frame_id,
    input wire [1:0] start_buffer_index,
    input wire in_valid,
    output wire in_ready,
    input wire [23:0] in_rgb,
    input wire in_last,
    output reg out_valid,
    input wire out_ready,
    output reg [511:0] out_data,
    output reg out_last,
    output reg [31:0] frame_id,
    output reg [1:0] buffer_index,
    output reg busy,
    output reg packing_done,
    output reg frame_error
);
    localparam integer PIXELS = 640 * 352;
    reg [17:0] pixel_count;
    reg [4:0] fill;
    reg [511:0] accum;
    reg finishing;
    function automatic [31:0] u8_fp32(input [7:0] value);
        integer bit_index, highest;
        reg [7:0] exponent;
        reg [31:0] fraction;
        begin
            highest = 0;
            for (bit_index = 0; bit_index < 8; bit_index = bit_index + 1)
            if (value[bit_index]) highest = bit_index;
            exponent = 127 + highest;
            fraction = {24'b0, value} << (23 - highest);
            u8_fp32  = (value == 0) ? 32'b0 : {1'b0, exponent, fraction[22:0]};
        end
    endfunction
    wire [31:0] red_fp = u8_fp32(in_rgb[23:16]);
    wire [31:0] green_fp = u8_fp32(in_rgb[15:8]);
    wire [31:0] blue_fp = u8_fp32(in_rgb[7:0]);
    wire [607:0] merged = {96'b0, accum} | ({512'b0, red_fp, green_fp, blue_fp} << (fill * 32));
    wire [5:0] total = {1'b0, fill} + 6'd3;
    assign in_ready = busy && !finishing && !abort && (!out_valid || out_ready);
    always @(posedge clk) begin
        if (!resetn) begin
            pixel_count <= 0;
            fill <= 0;
            accum <= 0;
            finishing <= 0;
            busy <= 0;
            packing_done <= 0;
            frame_error <= 0;
            out_valid <= 0;
            out_data <= 0;
            out_last <= 0;
            frame_id <= 0;
            buffer_index <= 0;
        end else begin
            packing_done <= 0;
            if (out_valid && out_ready) out_valid <= 0;
            if (!busy) begin
                if (start && !abort) begin
                    busy <= 1;
                    frame_error <= 0;
                    finishing <= 0;
                    pixel_count <= 0;
                    fill <= 0;
                    accum <= 0;
                    out_valid <= 0;
                    out_last <= 0;
                    frame_id <= start_frame_id;
                    buffer_index <= start_buffer_index;
                end
            end else if (abort) begin
                busy <= 0;
                frame_error <= 1;
                finishing <= 0;
                out_valid <= 0;
                out_last <= 0;
                fill <= 0;
                accum <= 0;
            end else if (finishing) begin
                // Completion is only logical output acceptance, never NPU/DDR visibility.
                if (out_valid && out_ready) begin
                    busy <= 0;
                    packing_done <= 1;
                    finishing <= 0;
                end
            end else if (in_valid && in_ready) begin
                if (in_last !== (pixel_count == PIXELS - 1)) begin
                    busy <= 0;
                    frame_error <= 1;
                    out_valid <= 0;
                    out_last <= 0;
                    fill <= 0;
                    accum <= 0;
                end else begin
                    pixel_count <= pixel_count + 1'b1;
                    if (total >= 16) begin
                        out_data <= merged[511:0];
                        out_valid <= 1;
                        out_last <= pixel_count == PIXELS - 1;
                        accum <= merged >> 512;
                        fill <= total - 16;
                    end else begin
                        accum <= merged[511:0];
                        fill  <= total[4:0];
                    end
                    if (pixel_count == PIXELS - 1) finishing <= 1;
                end
            end
        end
    end
endmodule
