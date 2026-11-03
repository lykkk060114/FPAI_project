`timescale 1ns / 1ps
// P3 OSD CSR bank and frame-boundary mailbox.
// Register page: 0x400C2000. Pixel coordinates are unsigned, inclusive,
// 11-bit x/y on a 1920x1080 image. A commit is atomically activated at VS.
module xj_osd_control (
    input wire gp_clk,
    input wire gp_resetn,
    input wire wr_valid,
    input wire [11:0] wr_addr,
    input wire [31:0] wr_data,
    input wire [11:0] rd_addr,
    output reg [31:0] rd_data,
    input wire video_clk,
    input wire video_resetn,
    input wire frame_start,
    output reg [521:0] active_payload,
    output wire [31:0] applied_frame_id
);
    reg [31:0] control_stage, frame_stage;
    reg [31:0] rect_xy0[0:7];
    reg [31:0] rect_xy1[0:7];
    reg [31:0] rect_color[0:7];
    reg busy, done, error, rejected;
    reg submit;
    reg [521:0] submit_payload;
    wire source_ready, source_rsp_valid;
    wire [31:0] source_rsp;
    wire dest_valid, dest_ready, dest_rsp_ready;
    wire [521:0] dest_payload;
    reg dest_pending, dest_rsp_valid;
    reg [521:0] dest_pending_payload;
    reg [31:0] dest_rsp_data;
    reg [60:0] rect_desc;
    reg [10:0] x0, y0, x1, y1;
    reg rect_valid;
    reg [2:0] rd_rect_idx, wr_rect_idx;
    reg [1:0] rd_rect_field, wr_rect_field;
    reg rd_rect_sel_valid, wr_rect_sel_valid;
    integer i;

    assign applied_frame_id = source_rsp;
    assign dest_ready = !dest_pending;

    // The CSR stride is 12 bytes, not a power of two.  Do not synthesize
    // division/modulo in the AXI write/read path: explicit address decoding
    // keeps the GP-to-OSD enable path shallow and preserves the byte offsets.
    always @* begin
        rd_rect_idx = 3'd0;
        rd_rect_field = 2'd0;
        rd_rect_sel_valid = 1'b0;
        case (rd_addr)
            12'h040: begin
                rd_rect_idx = 3'd0;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h044: begin
                rd_rect_idx = 3'd0;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h048: begin
                rd_rect_idx = 3'd0;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            12'h04C: begin
                rd_rect_idx = 3'd1;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h050: begin
                rd_rect_idx = 3'd1;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h054: begin
                rd_rect_idx = 3'd1;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            12'h058: begin
                rd_rect_idx = 3'd2;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h05C: begin
                rd_rect_idx = 3'd2;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h060: begin
                rd_rect_idx = 3'd2;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            12'h064: begin
                rd_rect_idx = 3'd3;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h068: begin
                rd_rect_idx = 3'd3;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h06C: begin
                rd_rect_idx = 3'd3;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            12'h070: begin
                rd_rect_idx = 3'd4;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h074: begin
                rd_rect_idx = 3'd4;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h078: begin
                rd_rect_idx = 3'd4;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            12'h07C: begin
                rd_rect_idx = 3'd5;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h080: begin
                rd_rect_idx = 3'd5;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h084: begin
                rd_rect_idx = 3'd5;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            12'h088: begin
                rd_rect_idx = 3'd6;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h08C: begin
                rd_rect_idx = 3'd6;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h090: begin
                rd_rect_idx = 3'd6;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            12'h094: begin
                rd_rect_idx = 3'd7;
                rd_rect_field = 2'd0;
                rd_rect_sel_valid = 1'b1;
            end
            12'h098: begin
                rd_rect_idx = 3'd7;
                rd_rect_field = 2'd1;
                rd_rect_sel_valid = 1'b1;
            end
            12'h09C: begin
                rd_rect_idx = 3'd7;
                rd_rect_field = 2'd2;
                rd_rect_sel_valid = 1'b1;
            end
            default: begin
            end
        endcase

        wr_rect_idx = 3'd0;
        wr_rect_field = 2'd0;
        wr_rect_sel_valid = 1'b0;
        case (wr_addr)
            12'h040: begin
                wr_rect_idx = 3'd0;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h044: begin
                wr_rect_idx = 3'd0;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h048: begin
                wr_rect_idx = 3'd0;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            12'h04C: begin
                wr_rect_idx = 3'd1;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h050: begin
                wr_rect_idx = 3'd1;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h054: begin
                wr_rect_idx = 3'd1;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            12'h058: begin
                wr_rect_idx = 3'd2;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h05C: begin
                wr_rect_idx = 3'd2;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h060: begin
                wr_rect_idx = 3'd2;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            12'h064: begin
                wr_rect_idx = 3'd3;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h068: begin
                wr_rect_idx = 3'd3;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h06C: begin
                wr_rect_idx = 3'd3;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            12'h070: begin
                wr_rect_idx = 3'd4;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h074: begin
                wr_rect_idx = 3'd4;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h078: begin
                wr_rect_idx = 3'd4;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            12'h07C: begin
                wr_rect_idx = 3'd5;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h080: begin
                wr_rect_idx = 3'd5;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h084: begin
                wr_rect_idx = 3'd5;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            12'h088: begin
                wr_rect_idx = 3'd6;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h08C: begin
                wr_rect_idx = 3'd6;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h090: begin
                wr_rect_idx = 3'd6;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            12'h094: begin
                wr_rect_idx = 3'd7;
                wr_rect_field = 2'd0;
                wr_rect_sel_valid = 1'b1;
            end
            12'h098: begin
                wr_rect_idx = 3'd7;
                wr_rect_field = 2'd1;
                wr_rect_sel_valid = 1'b1;
            end
            12'h09C: begin
                wr_rect_idx = 3'd7;
                wr_rect_field = 2'd2;
                wr_rect_sel_valid = 1'b1;
            end
            default: begin
            end
        endcase
    end

    xj_command_cdc #(
        .REQUEST_BITS (522),
        .RESPONSE_BITS(32)
    ) osd_frame_mailbox (
        .s_clk(gp_clk),
        .s_resetn(gp_resetn),
        .s_valid(submit),
        .s_ready(source_ready),
        .s_payload(submit_payload),
        .s_rsp_valid(source_rsp_valid),
        .s_rsp_ready(1'b1),
        .s_response(source_rsp),
        .m_clk(video_clk),
        .m_resetn(video_resetn),
        .m_valid(dest_valid),
        .m_ready(dest_ready),
        .m_payload(dest_payload),
        .m_rsp_valid(dest_rsp_valid),
        .m_rsp_ready(dest_rsp_ready),
        .m_response(dest_rsp_data)
    );

    always @* begin
        rd_data = 32'b0;
        case (rd_addr)
            12'h000: rd_data = 32'h584A4F01;
            12'h004: rd_data = {28'b0, rejected, error, done, busy};
            12'h00C: rd_data = control_stage;
            12'h010: rd_data = frame_stage;
            12'h014: rd_data = source_rsp;
            default: begin
                if (rd_rect_sel_valid) begin
                    case (rd_rect_field)
                        2'd0: rd_data = rect_xy0[rd_rect_idx];
                        2'd1: rd_data = rect_xy1[rd_rect_idx];
                        2'd2: rd_data = rect_color[rd_rect_idx];
                        default: rd_data = 0;
                    endcase
                end
            end
        endcase
    end

    always @(posedge gp_clk or negedge gp_resetn) begin
        if (!gp_resetn) begin
            control_stage <= 0;
            frame_stage <= 0;
            busy <= 0;
            done <= 0;
            error <= 0;
            rejected <= 0;
            submit <= 0;
            submit_payload <= 0;
            for (i = 0; i < 8; i = i + 1) begin
                rect_xy0[i]   <= 0;
                rect_xy1[i]   <= 0;
                rect_color[i] <= 0;
            end
        end else begin
            submit <= 0;
            if (source_rsp_valid) begin
                busy <= 0;
                done <= 1;
            end
            if (wr_valid) begin
                if (wr_addr == 12'h00C) begin
                    if ((wr_data & 32'hFFFFFFFC) != 0) begin
                        error <= 1;
                        rejected <= 1;
                    end else control_stage <= wr_data;
                end
                if (wr_addr == 12'h010) frame_stage <= wr_data;
                if (wr_rect_sel_valid) begin
                    case (wr_rect_field)
                        2'd0: rect_xy0[wr_rect_idx] <= wr_data;
                        2'd1: rect_xy1[wr_rect_idx] <= wr_data;
                        2'd2: rect_color[wr_rect_idx] <= wr_data;
                        default: begin
                        end
                    endcase
                end
                if (wr_addr == 12'h008) begin
                    if (wr_data[1] && !busy) begin
                        done <= 0;
                        error <= 0;
                        rejected <= 0;
                    end
                    if (wr_data[0]) begin
                        if (busy || submit || !source_ready) rejected <= 1;
                        else begin
                            submit_payload <= 0;
                            submit_payload[0] <= control_stage[0];
                            submit_payload[1] <= control_stage[1];
                            submit_payload[33:2] <= frame_stage;
                            for (i = 0; i < 8; i = i + 1) begin
                                x0 = rect_xy0[i][10:0];
                                y0 = rect_xy0[i][26:16];
                                x1 = rect_xy1[i][10:0];
                                y1 = rect_xy1[i][26:16];
                                rect_valid = rect_color[i][16];
                                // Clip right/bottom edges to the visible 1920x1080 image.
                                if (x1 > 11'd1919) x1 = 11'd1919;
                                if (y1 > 11'd1079) y1 = 11'd1079;
                                if (x0 > 11'd1919 || y0 > 11'd1079 || x1 < x0 || y1 < y0)
                                    rect_valid = 0;
                                rect_desc = {rect_valid, rect_color[i][15:0], y1, x1, y0, x0};
                                submit_payload[34+i*61+:61] <= rect_desc;
                            end
                            submit <= 1;
                            busy <= 1;
                            done <= 0;
                            error <= 0;
                            rejected <= 0;
                        end
                    end
                end
            end
        end
    end

    always @(posedge video_clk or negedge video_resetn) begin
        if (!video_resetn) begin
            dest_pending <= 0;
            dest_pending_payload <= 0;
            dest_rsp_valid <= 0;
            dest_rsp_data <= 0;
            active_payload <= 0;
        end else begin
            dest_rsp_valid <= 0;
            if (dest_valid && dest_ready) begin
                dest_pending <= 1;
                dest_pending_payload <= dest_payload;
            end
            if (dest_pending && frame_start && dest_rsp_ready) begin
                active_payload <= dest_pending_payload;
                dest_rsp_data  <= dest_pending_payload[33:2];
                dest_rsp_valid <= 1;
                dest_pending   <= 0;
            end
        end
    end
endmodule
