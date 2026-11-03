// Standalone logical memory reader + indexed Tensor writer, NOT a physical DMA.
// Read/response tags and descriptor ownership are enforced in this interface.
// Output is provisional until done; an error/reset invalidates all partial data.
module classifier_preprocess #(
    parameter NORMALIZE_FILE = "cp_normalize.hex"
) (
    input wire clk,
    resetn,
    start,
    output reg busy,
    done,
    error,
    input wire [31:0] cfg_x,
    cfg_y,
    cfg_w,
    cfg_h,
    cfg_padding,
    input wire [31:0] cfg_base,
    cfg_stride_bytes,
    cfg_capacity,
    cfg_frame_id,
    input wire [1:0] cfg_buffer_index,
    output reg rd_valid,
    input wire rd_ready,
    output reg [31:0] rd_addr,
    output wire [31:0] rd_frame_id,
    output wire [1:0] rd_buffer_index,
    input wire rsp_valid,
    output wire rsp_ready,
    input wire [15:0] rsp_data,
    input wire rsp_error,
    input wire [31:0] rsp_frame_id,
    output reg out_valid,
    input wire out_ready,
    output reg [31:0] out_data,
    output reg [17:0] out_word_index,
    output wire [31:0] out_frame_id,
    output wire [1:0] out_buffer_index,
    output reg out_last,
    output wire [11:0] roi_left,
    roi_top,
    roi_width,
    roi_height
);
    localparam IDLE=0,ROI=1,DIMS=2,DIV_START=3,DIV_WAIT=4,MAP_START=5,MAP_WAIT=6,
  PIXEL=7,READ_START=8,READ_ADDR=9,READ_WAIT=10,HORIZONTAL=11,VERTICAL=12,
  BYTE=13,LUT=14,OUTPUT_WORD=15,NEXT_PIXEL=16,READ_MULTIPLY=17,READ_SUM=18;
    reg [4:0] state;
    reg [31:0] base, stride, frame;
    reg [1:0] buffer_index;
    reg [31:0] box_x, box_y, box_w, box_h, box_padding;
    reg roi_start;
    wire roi_done, roi_error;
    reg dims_start;
    wire dims_done, dims_error, dims_busy;
    wire [7:0] resize_w, resize_h, pad_left, pad_top;
    reg [7:0] px, py;
    reg map_axis, map_start;
    reg [7:0] map_index;
    wire map_done;
    wire [11:0] map_lo, map_hi, map_a0, map_a1;
    reg [47:0] xmap[0:223], ymap[0:223];
    reg [47:0] xm, ym;
    reg [15:0] pixels[0:3];
    reg [1:0] sample_index, channel;
    reg [19:0] row0, row1;
    reg [31:0] v0, v1;
    reg [7:0] byte_value;
    reg [11:0] sample_x, sample_y;
    reg [31:0] row_offset;
    reg [31:0] norm[0:767];
    reg [15:0] pixel_number;
    wire [63:0] last_byte = 64'(cfg_base) + 64'(cfg_stride_bytes) * 1079 + 3839;
    assign rd_frame_id = frame;
    assign out_frame_id = frame;
    assign rd_buffer_index = buffer_index;
    assign out_buffer_index = buffer_index;
    assign rsp_ready = state == READ_WAIT;
    cp_roi roi_core (
        clk,
        resetn,
        roi_start,
        box_x,
        box_y,
        box_w,
        box_h,
        box_padding,
        roi_done,
        roi_error,
        roi_left,
        roi_top,
        roi_width,
        roi_height
    );
    cp_dimensions dimensions_core (
        clk,
        resetn,
        dims_start,
        roi_width,
        roi_height,
        dims_busy,
        dims_done,
        dims_error,
        resize_w,
        resize_h,
        pad_left,
        pad_top
    );
    cp_axis axis_core (
        clk,
        resetn,
        map_start,
        map_axis ? roi_height : roi_width,
        map_axis ? resize_h : resize_w,
        map_index,
        !map_axis,
        map_done,
        map_lo,
        map_hi,
        map_a0,
        map_a1
    );
    function automatic [7:0] component(input [15:0] p, input [1:0] ch);
        case (ch)
            0: component = {p[15:11], p[15:13]};
            1: component = {p[10:5], p[10:9]};
            default: component = {p[4:0], p[4:2]};
        endcase
    endfunction
    task automatic fail_frame;
        begin
            state <= IDLE;
            busy <= 0;
            done <= 1;
            error <= 1;
            rd_valid <= 0;
            out_valid <= 0;
            out_last <= 0;
        end
    endtask
    initial $readmemh(NORMALIZE_FILE, norm);
    always @(posedge clk)
        if (!resetn) begin
            state <= IDLE;
            busy <= 0;
            done <= 0;
            error <= 0;
            rd_valid <= 0;
            out_valid <= 0;
            out_last <= 0;
            roi_start <= 0;
            dims_start <= 0;
            map_start <= 0;
            out_data <= 0;
            out_word_index <= 0;
            base <= 0;
            stride <= 0;
            frame <= 0;
            buffer_index <= 0;
            rd_addr <= 0;
        end else begin
            done <= 0;
            roi_start <= 0;
            dims_start <= 0;
            map_start <= 0;
            case (state)
                IDLE:
                if (start) begin
                    error <= 0;
                    busy <= 1;
                    out_last <= 0;
                    if(cfg_base[0]||cfg_stride_bytes[0]||cfg_stride_bytes<3840||last_byte>64'hffffffff||
      64'(cfg_capacity)<64'(cfg_stride_bytes)*1079+3840)
                        fail_frame();
                    else begin
                        base <= cfg_base;
                        stride <= cfg_stride_bytes;
                        frame <= cfg_frame_id;
                        buffer_index <= cfg_buffer_index;
                        box_x <= cfg_x;
                        box_y <= cfg_y;
                        box_w <= cfg_w;
                        box_h <= cfg_h;
                        box_padding <= cfg_padding;
                        roi_start <= 1;
                        state <= ROI;
                    end
                end
                ROI:
                if (roi_done) begin
                    if (roi_error) fail_frame();
                    else state <= DIMS;
                end
                DIMS: begin
                    dims_start <= 1;
                    state <= DIV_WAIT;
                end
                DIV_WAIT:
                if (dims_done) begin
                    if (dims_error) fail_frame();
                    else begin
                        map_index <= 0;
                        map_axis <= 0;
                        state <= MAP_START;
                    end
                end
                MAP_START: begin
                    map_start <= 1;
                    state <= MAP_WAIT;
                end
                MAP_WAIT:
                if (map_done) begin
                    if (map_axis) ymap[map_index] <= {map_lo, map_hi, map_a0, map_a1};
                    else xmap[map_index] <= {map_lo, map_hi, map_a0, map_a1};
                    if (map_index + 1 == (map_axis ? resize_h : resize_w)) begin
                        map_index <= 0;
                        if (map_axis) begin
                            px <= 0;
                            py <= 0;
                            pixel_number <= 0;
                            state <= PIXEL;
                        end else begin
                            map_axis <= 1;
                            state <= MAP_START;
                        end
                    end else begin
                        map_index <= map_index + 1;
                        state <= MAP_START;
                    end
                end
                PIXEL: begin
                    channel  <= 0;
                    out_last <= 0;
                    if(px<pad_left||px>=pad_left+resize_w||py<pad_top||py>=pad_top+resize_h) begin
                        byte_value <= 127;
                        state <= LUT;
                    end else begin
                        xm <= xmap[px-pad_left];
                        ym <= ymap[py-pad_top];
                        sample_index <= 0;
                        state <= READ_START;
                    end
                end
                READ_START: begin
                    sample_y <= roi_top + (sample_index[1] ? ym[35:24] : ym[47:36]);
                    sample_x <= roi_left + (sample_index[0] ? xm[35:24] : xm[47:36]);
                    state <= READ_MULTIPLY;
                end
                READ_MULTIPLY: begin
                    row_offset <= sample_y * stride;
                    state <= READ_SUM;
                end
                READ_SUM: begin
                    rd_addr <= base + row_offset + {sample_x, 1'b0};
                    rd_valid <= 1;
                    state <= READ_ADDR;
                end
                READ_ADDR:
                if (rd_ready) begin
                    rd_valid <= 0;
                    state <= READ_WAIT;
                end
                READ_WAIT:
                if (rsp_valid) begin
                    if (rsp_error || rsp_frame_id != frame) fail_frame();
                    else begin
                        pixels[sample_index] <= rsp_data;
                        if (sample_index == 3) state <= HORIZONTAL;
                        else begin
                            sample_index <= sample_index + 1;
                            state <= READ_START;
                        end
                    end
                end
                HORIZONTAL: begin
                    row0 <= component(
                        pixels[0], channel
                    ) * xm[23:12] + component(
                        pixels[1], channel
                    ) * xm[11:0];
                    row1 <= component(
                        pixels[2], channel
                    ) * xm[23:12] + component(
                        pixels[3], channel
                    ) * xm[11:0];
                    state <= VERTICAL;
                end
                VERTICAL: begin
                    v0 <= (row0 >> 4) * ym[23:12];
                    v1 <= (row1 >> 4) * ym[11:0];
                    state <= BYTE;
                end
                BYTE: begin
                    byte_value <= ((v0 >> 16) + (v1 >> 16) + 2) >> 2;
                    state <= LUT;
                end
                LUT: begin
                    out_data <= norm[{channel, 8'b0}+byte_value];
                    out_word_index <= pixel_number * 3 + channel;
                    out_last <= pixel_number == 50175 && channel == 2;
                    out_valid <= 1;
                    state <= OUTPUT_WORD;
                end
                OUTPUT_WORD:
                if (out_ready) begin
                    out_valid <= 0;
                    if (channel == 2) state <= NEXT_PIXEL;
                    else begin
                        channel <= channel + 1;
                        if(px<pad_left||px>=pad_left+resize_w||py<pad_top||py>=pad_top+resize_h)
                            state <= LUT;
                        else state <= HORIZONTAL;
                    end
                end
                NEXT_PIXEL:
                if (pixel_number == 50175) begin
                    done  <= 1;
                    busy  <= 0;
                    state <= IDLE;
                end else begin
                    pixel_number <= pixel_number + 1;
                    if (px == 223) begin
                        px <= 0;
                        py <= py + 1;
                    end else px <= px + 1;
                    state <= PIXEL;
                end
            endcase
        end
endmodule
