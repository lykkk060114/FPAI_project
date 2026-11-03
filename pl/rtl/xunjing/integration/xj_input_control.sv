`timescale 1ns / 1ps
// Official RGBu8 resize -> BGR FP32 NHWC -> combined 512-bit User_Ddr.
// Inferred transport: BYTE address, 64-byte increment, info6=0, byte enables=1.
// No write response exists on this interface. Status bit5 is acceptance only;
// software issues CONTROL.COMMIT after its runtime visibility barrier.
module xj_input_control (
    input wire gp_clk,
    gp_resetn,
    hp_clk,
    hp_resetn,
    cam_clk,
    cam_reset,
    input wire vs,
    de,
    input wire [23:0] rgb,
    input wire wr_valid,
    input wire [11:0] wr_addr,
    input wire [31:0] wr_data,
    input wire [11:0] rd_addr,
    output reg [31:0] rd_data,
    output wire [31:0] awaddr,
    output wire [511:0] awdata,
    output wire awvalid,
    input wire awready
);
    localparam PIXELS = 225280, BYTES = 2703360;
    reg [31:0] cfg[0:5];  // base,capacity,frame_id,buffer,timeout_hp,settle_hp
    reg send, busy, done, error, rejected, commit_level, abort_level;
    reg [7:0] reason;
    reg [31:0] accepted_count;
    // Camera-domain diagnostics. Counters cross to hp_clk as Gray codes so the
    // software readback is meaningful without changing the capture protocol.
    reg [31:0] diag_vs_count_bin, diag_end_count_bin, diag_fifo_full_bin;
    reg [17:0] diag_last_de_count_bin;
    reg [8:0] diag_status_cam;
    wire cmd_ready, result_valid;
    wire [39:0] result;
    wire hv, hr;
    wire [191:0] hp;
    wire hrv, hrr;
    wire [39:0] hresponse;
    xj_command_cdc #(
        .REQUEST_BITS (192),
        .RESPONSE_BITS(40)
    ) config_cdc (
        .s_clk(gp_clk),
        .s_resetn(gp_resetn),
        .s_valid(send),
        .s_ready(cmd_ready),
        .s_payload({cfg[5], cfg[4], cfg[3], cfg[2], cfg[1], cfg[0]}),
        .s_rsp_valid(result_valid),
        .s_rsp_ready(1'b1),
        .s_response(result),
        .m_clk(hp_clk),
        .m_resetn(hp_resetn),
        .m_valid(hv),
        .m_ready(hr),
        .m_payload(hp),
        .m_rsp_valid(hrv),
        .m_rsp_ready(hrr),
        .m_response(hresponse)
    );
    reg accepted_hp;
    (* ASYNC_REG="TRUE" *) reg [1:0] accepted_sync, commit_sync, abort_sync;
    integer i;
    function automatic [31:0] gray2bin32(input [31:0] g);
        integer k;
        begin
            gray2bin32[31] = g[31];
            for (k = 30; k >= 0; k = k - 1) gray2bin32[k] = gray2bin32[k+1] ^ g[k];
        end
    endfunction
    function automatic [17:0] gray2bin18(input [17:0] g);
        integer k;
        begin
            gray2bin18[17] = g[17];
            for (k = 16; k >= 0; k = k - 1) gray2bin18[k] = gray2bin18[k+1] ^ g[k];
        end
    endfunction
    function automatic [31:0] bin2gray32(input [31:0] b);
        begin
            bin2gray32 = b ^ (b >> 1);
        end
    endfunction
    function automatic [17:0] bin2gray18(input [17:0] b);
        begin
            bin2gray18 = b ^ (b >> 1);
        end
    endfunction
    reg [31:0] diag_vs_gray_cam, diag_end_gray_cam, diag_fifo_gray_cam;
    reg [17:0] diag_last_de_gray_cam;
    (* ASYNC_REG="TRUE" *) reg [31:0] diag_vs_gray_hp1, diag_vs_gray_hp2;
    (* ASYNC_REG="TRUE" *) reg [31:0] diag_end_gray_hp1, diag_end_gray_hp2;
    (* ASYNC_REG="TRUE" *) reg [31:0] diag_fifo_gray_hp1, diag_fifo_gray_hp2;
    (* ASYNC_REG="TRUE" *) reg [17:0] diag_last_de_gray_hp1, diag_last_de_gray_hp2;
    (* ASYNC_REG="TRUE" *) reg [8:0] diag_status_hp1, diag_status_hp2;
    always @* begin
        rd_data = 0;
        case (rd_addr)
            0: rd_data = 32'h584a4901;
            4:
            rd_data = {
                16'b0,
                reason,
                2'b0,
                accepted_sync[1],
                (gp_resetn && !busy && !send),
                rejected,
                error,
                done,
                busy
            };
            12'h024: rd_data = accepted_count;
            12'h028: rd_data = 32'd42240;
            12'h02c: rd_data = BYTES;
            12'h030: rd_data = gray2bin32(diag_vs_gray_hp2);
            12'h034: rd_data = gray2bin32(diag_end_gray_hp2);
            12'h038: rd_data = {14'b0, gray2bin18(diag_last_de_gray_hp2)};
            12'h03c: rd_data = gray2bin32(diag_fifo_gray_hp2);
            12'h040: rd_data = {23'b0, diag_status_hp2};
            default:
            if (rd_addr >= 12'h00c && rd_addr <= 12'h020 && rd_addr[1:0] == 0)
                rd_data = cfg[(rd_addr-12'h00c)>>2];
        endcase
    end
    always @(posedge gp_clk) begin
        if (!gp_resetn) begin
            send <= 0;
            busy <= 0;
            done <= 0;
            error <= 0;
            rejected <= 0;
            reason <= 0;
            commit_level <= 0;
            abort_level <= 0;
            accepted_sync <= 0;
            accepted_count <= 0;
            diag_vs_gray_hp1 <= 0;
            diag_vs_gray_hp2 <= 0;
            diag_end_gray_hp1 <= 0;
            diag_end_gray_hp2 <= 0;
            diag_fifo_gray_hp1 <= 0;
            diag_fifo_gray_hp2 <= 0;
            diag_last_de_gray_hp1 <= 0;
            diag_last_de_gray_hp2 <= 0;
            diag_status_hp1 <= 0;
            diag_status_hp2 <= 0;
            for (i = 0; i < 6; i = i + 1) cfg[i] <= 0;
        end else begin
            accepted_sync <= {accepted_sync[0], accepted_hp};
            diag_vs_gray_hp1 <= diag_vs_gray_cam;
            diag_vs_gray_hp2 <= diag_vs_gray_hp1;
            diag_end_gray_hp1 <= diag_end_gray_cam;
            diag_end_gray_hp2 <= diag_end_gray_hp1;
            diag_fifo_gray_hp1 <= diag_fifo_gray_cam;
            diag_fifo_gray_hp2 <= diag_fifo_gray_hp1;
            diag_last_de_gray_hp1 <= diag_last_de_gray_cam;
            diag_last_de_gray_hp2 <= diag_last_de_gray_hp1;
            diag_status_hp1 <= diag_status_cam;
            diag_status_hp2 <= diag_status_hp1;
            if (accepted_sync[1] && busy) accepted_count <= 32'd42240;
            if (send && cmd_ready) send <= 0;
            if (wr_valid) begin
                if (wr_addr >= 12'h00c && wr_addr <= 12'h020 && wr_addr[1:0] == 0) begin
                    if (!busy) cfg[(wr_addr-12'h00c)>>2] <= wr_data;
                    else rejected <= 1;
                end
                if (wr_addr == 8) begin
                    if (wr_data[1] && !busy) begin
                        done <= 0;
                        error <= 0;
                        reason <= 0;
                        rejected <= 0;
                    end
                    if (wr_data[2] && busy) abort_level <= 1;
                    if (wr_data[3] && busy && accepted_sync[1]) commit_level <= 1;
                    if (wr_data[0]) begin
                        if (busy) rejected <= 1;
                        else begin
                            done <= 0;
                            error <= 0;
                            reason <= 0;
                            rejected <= 0;
                            accepted_count <= 0;
                            commit_level <= 0;
                            abort_level <= 0;
                            if(cfg[0][5:0]!=0||cfg[1]<BYTES||cfg[3]>3||cfg[4]==0||
          {1'b0,cfg[0]}+33'd2703360>33'h100000000)begin
                                done   <= 1;
                                error  <= 1;
                                reason <= 1;
                            end else begin
                                send <= 1;
                                busy <= 1;
                            end
                        end
                    end
                end
            end
            if (result_valid) begin
                busy <= 0;
                done <= 1;
                reason <= result[39:32];
                error <= result[39:32] != 0;
                accepted_count <= result[31:0];
            end
        end
    end

    wire cam_resetn;
    xj_reset_release camera_path_reset (
        .clk(cam_clk),
        .async_reset(!hp_resetn),
        .resetn(cam_resetn)
    );
    reg arm_send;
    wire arm_ready, cap_result_valid;
    wire [18:0] cap_result;
    wire arm_valid, cap_rsp_ready;
    reg cap_rsp_valid;
    reg [18:0] cap_response;
    reg capturing, waiting, vs_d, cap_bad, stop_hp;
    reg [17:0] cap_count;
    (* ASYNC_REG="TRUE" *) reg [1:0] stop_sync;
    wire capture_ready = !capturing && !waiting && !cap_rsp_valid && !stop_sync[1];
    xj_command_cdc #(
        .REQUEST_BITS (1),
        .RESPONSE_BITS(19)
    ) capture_cdc (
        .s_clk(hp_clk),
        .s_resetn(hp_resetn),
        .s_valid(arm_send),
        .s_ready(arm_ready),
        .s_payload(1'b1),
        .s_rsp_valid(cap_result_valid),
        .s_rsp_ready(1'b1),
        .s_response(cap_result),
        .m_clk(cam_clk),
        .m_resetn(cam_resetn),
        .m_valid(arm_valid),
        .m_ready(capture_ready),
        .m_payload(),
        .m_rsp_valid(cap_rsp_valid),
        .m_rsp_ready(cap_rsp_ready),
        .m_response(cap_response)
    );
    wire fifo_wr_ready, fifo_valid, fifo_ready;
    wire [23:0] fifo_data;
    wire new_vs = vs && !vs_d;
    wire capture_pixel = capturing && de && !new_vs && !cap_bad && !cam_reset && !stop_sync[1];
    xj_pixel_fifo pixels (
        .wclk  (cam_clk),
        .wrstn (cam_resetn),
        .wvalid(capture_pixel),
        .wdata (rgb),
        .wready(fifo_wr_ready),
        .rclk  (hp_clk),
        .rrstn (hp_resetn),
        .rvalid(fifo_valid),
        .rready(fifo_ready),
        .rdata (fifo_data)
    );
    always @(posedge cam_clk) begin
        if (!cam_resetn) begin
            capturing <= 0;
            waiting <= 0;
            vs_d <= 0;
            cap_bad <= 0;
            cap_count <= 0;
            cap_rsp_valid <= 0;
            cap_response <= 0;
            stop_sync <= 0;
            diag_vs_count_bin <= 0;
            diag_end_count_bin <= 0;
            diag_fifo_full_bin <= 0;
            diag_last_de_count_bin <= 0;
            diag_status_cam <= 0;
            diag_vs_gray_cam <= 0;
            diag_end_gray_cam <= 0;
            diag_fifo_gray_cam <= 0;
            diag_last_de_gray_cam <= 0;
        end else begin
            vs_d <= vs;
            stop_sync <= {stop_sync[0], stop_hp};
            diag_status_cam <= {
                stop_sync[1],
                cap_bad,
                fifo_wr_ready,
                de,
                vs,
                capturing,
                waiting,
                cam_resetn,
                cam_reset
            };
            if (new_vs) begin
                diag_vs_count_bin <= diag_vs_count_bin + 1'b1;
                diag_vs_gray_cam  <= bin2gray32(diag_vs_count_bin + 1'b1);
            end
            if (cap_rsp_valid && cap_rsp_ready) cap_rsp_valid <= 0;
            if (arm_valid && capture_ready) begin
                waiting   <= 1;
                cap_bad   <= 0;
                cap_count <= 0;
            end
            if (waiting && new_vs) begin
                waiting   <= 0;
                capturing <= 1;
            end
            if((waiting||capturing)&&(cam_reset||stop_sync[1]||(capturing&&new_vs)))begin
                waiting <= 0;
                capturing <= 0;
                cap_rsp_valid <= 1;
                cap_response <= {1'b1, cap_count};
                diag_end_count_bin <= diag_end_count_bin + 1'b1;
                diag_end_gray_cam <= bin2gray32(diag_end_count_bin + 1'b1);
                diag_last_de_count_bin <= cap_count;
                diag_last_de_gray_cam <= bin2gray18(cap_count);
            end else if (capturing && de) begin
                if (!fifo_wr_ready) begin
                    cap_bad <= 1;
                    diag_fifo_full_bin <= diag_fifo_full_bin + 1'b1;
                    diag_fifo_gray_cam <= bin2gray32(diag_fifo_full_bin + 1'b1);
                end
                if (cap_count == PIXELS - 1) begin
                    capturing <= 0;
                    cap_rsp_valid <= 1;
                    cap_response <= {cap_bad || !fifo_wr_ready, 18'd225280};
                    diag_end_count_bin <= diag_end_count_bin + 1'b1;
                    diag_end_gray_cam <= bin2gray32(diag_end_count_bin + 1'b1);
                    diag_last_de_count_bin <= 18'd225280;
                    diag_last_de_gray_cam <= bin2gray18(18'd225280);
                end else cap_count <= cap_count + 1'b1;
            end
        end
    end

    reg [2:0] state;
    reg [31:0] base, frame, timeout_limit, settle_limit, ticks, beats, settle;
    reg [1:0] buffer;
    reg [17:0] fed;
    reg fmt_start, fmt_abort, failed, cap_finished;
    reg [7:0] hp_reason;
    reg [3:0] quiet;
    wire fmt_in_ready, fmt_valid, fmt_last, fmt_done, fmt_error;
    wire [511:0] fmt_data;
    assign hr = state == 0;
    assign hrv = state == 5;
    assign hresponse = {hp_reason, beats};
    assign awaddr = base + (beats << 6);
    assign awdata = fmt_data;
    assign awvalid = fmt_valid;
    wire feeding = state == 2 && !failed;
    assign fifo_ready = feeding ? fmt_in_ready : state == 4;
    rgbu8_bgr_fp32_pack formatter (
        .clk(hp_clk),
        .resetn(hp_resetn),
        .start(fmt_start),
        .abort(fmt_abort),
        .start_frame_id(frame),
        .start_buffer_index(buffer),
        .in_valid(fifo_valid && feeding),
        .in_ready(fmt_in_ready),
        .in_rgb(fifo_data),
        .in_last(fed == PIXELS - 1),
        .out_valid(fmt_valid),
        .out_ready(awready),
        .out_data(fmt_data),
        .out_last(fmt_last),
        .frame_id(),
        .buffer_index(),
        .busy(),
        .packing_done(fmt_done),
        .frame_error(fmt_error)
    );
    always @(posedge hp_clk) begin
        if (!hp_resetn) begin
            state <= 0;
            base <= 0;
            frame <= 0;
            buffer <= 0;
            timeout_limit <= 0;
            settle_limit <= 0;
            ticks <= 0;
            beats <= 0;
            settle <= 0;
            fed <= 0;
            fmt_start <= 0;
            fmt_abort <= 0;
            failed <= 0;
            cap_finished <= 0;
            hp_reason <= 0;
            quiet <= 0;
            accepted_hp <= 0;
            arm_send <= 0;
            stop_hp <= 0;
            commit_sync <= 0;
            abort_sync <= 0;
        end else begin
            fmt_start   <= 0;
            fmt_abort   <= 0;
            commit_sync <= {commit_sync[0], commit_level};
            abort_sync  <= {abort_sync[0], abort_level};
            if (arm_send && arm_ready) arm_send <= 0;
            if (cap_result_valid) begin
                cap_finished <= 1;
                if (cap_result[18] || cap_result[17:0] != PIXELS) begin
                    failed <= 1;
                    hp_reason <= 2;
                end
            end
            if (awvalid && awready) beats <= beats + 1'b1;
            if (fifo_valid && fifo_ready && feeding) fed <= fed + 1'b1;
            if (state != 0 && state != 5 && state != 4) begin
                if (ticks < timeout_limit) ticks <= ticks + 1'b1;
                else begin
                    failed <= 1;
                    hp_reason <= 3;
                end
                // State 1 deliberately waits for the preceding command levels to clear.
                // The formatter also clears its sticky prior-frame error on fmt_start.
                if (state != 1 && abort_sync[1]) begin
                    failed <= 1;
                    hp_reason <= 4;
                end
                if (state != 1 && fmt_error) begin
                    failed <= 1;
                    hp_reason <= 5;
                end
            end
            case (state)
                0:
                if (hv) begin
                    base <= hp[31:0];
                    frame <= hp[95:64];
                    buffer <= hp[97:96];
                    timeout_limit <= hp[159:128];
                    settle_limit <= hp[191:160];
                    ticks <= 0;
                    beats <= 0;
                    fed <= 0;
                    failed <= 0;
                    hp_reason <= 0;
                    cap_finished <= 0;
                    accepted_hp <= 0;
                    stop_hp <= 0;
                    fmt_start <= 1;
                    state <= 1;
                end
                // Allow stale GP abort/commit levels to clear before arming a new frame.
                1:
                if (!commit_sync[1] && !abort_sync[1]) begin
                    arm_send <= 1;
                    state <= 2;
                end
                2: begin
                    if (failed) begin
                        stop_hp <= 1;
                        state   <= 4;
                        quiet   <= 0;
                    end else if (fmt_done && beats == 42240) begin
                        state  <= 3;
                        settle <= 0;
                    end
                end
                3: begin
                    if (failed) begin
                        stop_hp <= 1;
                        state   <= 4;
                        quiet   <= 0;
                    end else if (cap_finished) begin
                        if (settle < settle_limit) settle <= settle + 1'b1;
                        else begin
                            accepted_hp <= 1;
                            if (commit_sync[1]) begin
                                hp_reason <= 0;
                                state <= 5;
                            end
                        end
                    end
                end
                4: begin
                    // Never withdraw an already presented combined write under backpressure.
                    if (!fmt_valid) fmt_abort <= 1;
                    if (cap_finished && !fifo_valid && !fmt_valid) begin
                        if (quiet == 8) state <= 5;
                        else quiet <= quiet + 1'b1;
                    end else quiet <= 0;
                end
                5:
                if (hrr) begin
                    state <= 0;
                    accepted_hp <= 0;
                end
            endcase
        end
    end
endmodule
