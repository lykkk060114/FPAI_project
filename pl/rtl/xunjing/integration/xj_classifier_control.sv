`timescale 1ns / 1ps
// CLASS controller: read the final RGB565 ROI through HP2, then emit the
// normalized RGB FP32 tensor in NHWC order to the native User_Ddr write port.
// Addresses and cache visibility remain runtime-owned by PS.
module xj_classifier_control #(
    parameter NORMALIZE_FILE = "cp_normalize.hex"
) (
    input wire clk,
    resetn,
    input wire wr_valid,
    input wire [11:0] wr_addr,
    input wire [31:0] wr_data,
    input wire [11:0] rd_addr,
    output reg [31:0] rd_data,
    output wire mem_valid,
    input wire mem_ready,
    output wire mem_write,
    output wire [31:0] mem_addr,
    output wire [31:0] mem_data,
    input wire mem_rsp_valid,
    output wire mem_rsp_ready,
    input wire [15:0] mem_rsp_data,
    input wire mem_rsp_error,
    output wire ddr_awvalid,
    input wire ddr_awready,
    output wire [31:0] ddr_awaddr,
    output wire [511:0] ddr_awdata,
    input wire ddr_rsp_valid,
    input wire ddr_rsp_error,
    output wire ddr_rsp_ready
);
    reg [31:0] staging[0:12], job[0:12];
    reg busy, done, error, failed, rejected, core_start, core_finished;
    reg pending;
    reg [31:0] pending_addr, error_addr;
    reg [17:0] written;
    reg [7:0] reason;
    wire c_busy,c_done,c_error,c_rd_valid,c_rd_ready,c_rsp_ready,c_out_valid,c_out_ready,c_last;
    wire [31:0] c_rd_addr, c_rd_frame, c_out_frame, c_out_data;
    wire [1:0] c_rd_buffer, c_out_buffer;
    wire [17:0] c_index;
    wire cancel_requested = wr_valid && wr_addr == 12'h008 && wr_data[2];
    wire [63:0] src_end = 64'(staging[1]) + 64'(staging[2]) * 1079 + 3840;
    wire [63:0] dst_end = 64'(staging[10]) + 602112;
    reg [511:0] pack_data;
    reg [3:0] pack_count;
    reg [17:0] beat_count, expected_index;
    reg ddr_pending, ddr_sent;
    assign mem_valid = busy && !failed && !pending && c_rd_valid;
    assign mem_write = 1'b0;
    assign mem_addr = c_rd_addr;
    assign mem_data = 0;
    assign mem_rsp_ready = pending;
    assign ddr_awvalid = ddr_pending && !ddr_sent;
    assign ddr_awaddr = job[10] + ({14'b0, beat_count, 6'b0});
    assign ddr_awdata = pack_data;
    assign ddr_rsp_ready = 1'b1;
    assign c_rd_ready = mem_ready && mem_valid;
    // Hold the output stream while a source read or a native write is in flight.
    assign c_out_ready = !ddr_pending && !c_rd_valid;
    function automatic [31:0] uint_to_fp32(input [31:0] value);
        integer bitno, k;
        reg [31:0] mantissa;
        begin
            bitno = 0;
            for (k = 0; k < 32; k = k + 1) if (value[k]) bitno = k;
            mantissa = value << (23 - bitno);
            uint_to_fp32 = value == 0 ? 0 : ({1'b0, (8'd127 + bitno[7:0]), mantissa[22:0]});
        end
    endfunction
    classifier_preprocess #(
        .NORMALIZE_FILE(NORMALIZE_FILE)
    ) core (
        .clk(clk),
        .resetn(resetn && busy && !failed),
        .start(core_start),
        .busy(c_busy),
        .done(c_done),
        .error(c_error),
        .cfg_frame_id(job[0]),
        .cfg_base(job[1]),
        .cfg_stride_bytes(job[2]),
        .cfg_capacity(job[3]),
        .cfg_x(uint_to_fp32(job[4])),
        .cfg_y(uint_to_fp32(job[5])),
        .cfg_w(uint_to_fp32(job[6])),
        .cfg_h(uint_to_fp32(job[7])),
        .cfg_padding(32'b0),
        .cfg_buffer_index(job[12][1:0]),
        .rd_valid(c_rd_valid),
        .rd_ready(c_rd_ready),
        .rd_addr(c_rd_addr),
        .rd_frame_id(c_rd_frame),
        .rd_buffer_index(c_rd_buffer),
        .rsp_valid(mem_rsp_valid && pending && !failed),
        .rsp_ready(c_rsp_ready),
        .rsp_data(mem_rsp_data),
        .rsp_error(mem_rsp_error),
        .rsp_frame_id(job[0]),
        .out_valid(c_out_valid),
        .out_ready(c_out_ready),
        .out_data(c_out_data),
        .out_word_index(c_index),
        .out_frame_id(c_out_frame),
        .out_buffer_index(c_out_buffer),
        .out_last(c_last),
        .roi_left(),
        .roi_top(),
        .roi_width(),
        .roi_height()
    );
    integer i;
    always @* begin
        rd_data = 0;
        case (rd_addr)
            12'h000: rd_data = 32'h584a4302;
            12'h004:
            rd_data = {16'b0, reason, 3'b0, (resetn && !busy), rejected, error, done, busy};
            12'h040: rd_data = {14'b0, written};
            12'h044: rd_data = error_addr;
            12'h048:
            rd_data = 32'h00000003;  // NHWC RGB FP32, native User_Ddr writes; no cache claim.
            default:
            if (rd_addr >= 12'h00c && rd_addr <= 12'h03c && rd_addr[1:0] == 0)
                rd_data = staging[(rd_addr-12'h00c)>>2];
        endcase
    end
    always @(posedge clk) begin
        if (!resetn) begin
            busy <= 0;
            done <= 0;
            error <= 0;
            failed <= 0;
            rejected <= 0;
            core_start <= 0;
            core_finished <= 0;
            pending <= 0;
            pending_addr <= 0;
            error_addr <= 0;
            written <= 0;
            reason <= 0;
            pack_data <= 0;
            pack_count <= 0;
            beat_count <= 0;
            expected_index <= 0;
            ddr_pending <= 0;
            ddr_sent <= 0;
            for (i = 0; i < 13; i = i + 1) begin
                staging[i] <= 0;
                job[i] <= 0;
            end
        end else begin
            core_start <= 0;
            if (ddr_awvalid && ddr_awready) ddr_sent <= 1;
            if (ddr_pending && ddr_rsp_valid && ddr_rsp_ready) begin
                if (ddr_rsp_error) begin
                    failed <= 1;
                    reason <= 8'h02;
                    error_addr <= ddr_awaddr;
                end
                ddr_sent <= 0;
                ddr_pending <= 0;
                written <= written + 1'b1;
                beat_count <= beat_count + 1'b1;
                pack_data <= 0;
            end
            if (wr_valid && wr_addr >= 12'h00c && wr_addr <= 12'h03c && wr_addr[1:0] == 0)
                staging[(wr_addr-12'h00c)>>2] <= wr_data;
            if (wr_valid && wr_addr == 12'h008) begin
                if (wr_data[1] && !busy) begin
                    done <= 0;
                    error <= 0;
                    rejected <= 0;
                    reason <= 0;
                end
                if (wr_data[2] && busy) begin
                    failed <= 1;
                    reason <= 8'h04;
                end
                if (wr_data[0]) begin
                    if (busy) rejected <= 1;
                    else begin
                        done <= 0;
                        error <= 0;
                        failed <= 0;
                        rejected <= 0;
                        reason <= 0;
                        error_addr <= 0;
                        written <= 0;
                        core_finished <= 0;
                        pack_data <= 0;
                        pack_count <= 0;
                        beat_count <= 0;
                        expected_index <= 0;
                        ddr_pending <= 0;
                        ddr_sent <= 0;
                        if(staging[12]>3||staging[1][2:0]!=0||staging[2][2:0]!=0||staging[2]<3840||
        staging[10][5:0]!=0||staging[11]<602112||src_end>64'h40000000||
        64'(staging[3])<64'(staging[2])*1079+3840||dst_end>64'h100000000||
        staging[4]>=1920||staging[5]>=1080||staging[6]==0||staging[7]==0||
        64'(staging[4])+staging[6]>1920||64'(staging[5])+staging[7]>1080||
        staging[8]!=0||staging[9]!=0)begin
                            done   <= 1;
                            error  <= 1;
                            reason <= 8'h01;
                        end else begin
                            for (i = 0; i < 13; i = i + 1) job[i] <= staging[i];
                            busy <= 1;
                            core_start <= 1;
                        end
                    end
                end
            end
            if (busy) begin
                if (mem_valid && mem_ready) begin
                    pending <= 1;
                    pending_addr <= mem_addr;
                end
                if (mem_rsp_valid && mem_rsp_ready) begin
                    pending <= 0;
                    if (mem_rsp_error) begin
                        failed <= 1;
                        reason <= 8'h02;
                        error_addr <= pending_addr;
                    end
                end
                if (c_out_valid && c_out_ready) begin
                    if(c_index!=expected_index||c_index>=150528||c_last!=(expected_index==150527))begin
                        failed <= 1;
                        reason <= 8'h05;
                    end else begin
                        expected_index <= expected_index + 1'b1;
                        if (pack_count == 4'd15) begin
                            pack_data <= pack_data | ({480'b0, c_out_data} << 480);
                            ddr_pending <= 1;
                            ddr_sent <= 0;
                            pack_count <= 0;
                        end else begin
                            pack_data  <= pack_data | ({480'b0, c_out_data} << (pack_count * 32));
                            pack_count <= pack_count + 1'b1;
                        end
                    end
                end
                if (c_done) begin
                    core_finished <= 1;
                    if (c_error) begin
                        failed <= 1;
                        reason <= 8'h03;
                    end
                end
                if (failed && !pending && !ddr_pending) begin
                    busy  <= 0;
                    done  <= 1;
                    error <= 1;
                end
    else if(core_finished&&!pending&&!ddr_pending&&!c_error&&!cancel_requested)begin
                    busy  <= 0;
                    done  <= 1;
                    error <= written != 18'd9408;
                    if (written != 18'd9408) reason <= 8'h06;
                end
            end
        end
    end
endmodule
