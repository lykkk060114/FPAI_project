`timescale 1ns / 1ps
// Decode/NMS controller with descriptor-based input and PS DDR output ports.
module xj_postprocess_control (
    input  wire        clk,
    input  wire        resetn,
    input  wire        wr_valid,
    input  wire [11:0] wr_addr,
    input  wire [31:0] wr_data,
    input  wire [11:0] rd_addr,
    output reg  [31:0] rd_data,

    output wire         mem_valid,
    input  wire         mem_ready,
    output wire [ 31:0] mem_addr,
    input  wire         mem_rsp_valid,
    output wire         mem_rsp_ready,
    input  wire [511:0] mem_rsp_data,
    input  wire [ 31:0] mem_rsp_info,
    input  wire         mem_rsp_error,

    // PS DDR port used for result writes.
    output wire        ps_valid,
    input  wire        ps_ready,
    output wire [31:0] ps_addr,
    output wire [31:0] ps_data,
    input  wire        ps_rsp_valid,
    output wire        ps_rsp_ready,
    input  wire        ps_rsp_error
);
    reg [31:0] cfg[0:8];
    reg [31:0] job[0:8];

    // Descriptor fields: base, extent, grid stride, and optional channel stride.
    reg [31:0] desc[0:23];
    reg [31:0] jd[0:23];

    reg [31:0] confidence;
    reg [31:0] job_confidence;
    reg [31:0] result_base;
    reg [31:0] result_capacity;
    reg [31:0] timeout_cycles;
    reg [31:0] job_result_base;
    reg [31:0] job_timeout;
    reg [31:0] elapsed;

    reg busy;
    reg done;
    reg error;
    reg failed;
    reg rejected;
    reg core_start;
    reg [7:0] reason;
    reg [31:0] last_info;
    reg [31:0] error_addr;
    reg [3:0] reset_lease;
    reg [1:0] terminal_hold;
    reg finish_error;
    reg [12:0] committed_candidates;

    reg validating;
    reg core_started;
    reg [2:0] validate_index;
    reg [1:0] validate_state;
    reg [63:0] validate_base;
    reg [63:0] validate_extent;
    reg [63:0] validate_last;
    reg [43:0] validate_product;
    reg [33:0] validate_extra;
    reg validate_bad;
    wire [4:0] vi = {validate_index, 2'b0};
    wire [11:0] tail_count = validate_index < 2 ? 12'd3519 : validate_index < 4 ? 12'd879 : 12'd219;

    reg diag_busy;
    reg diag_done;
    reg diag_error;
    reg diag_send;
    reg [31:0] diag_addr;
    reg [31:0] diag_info;
    reg [511:0] diag_data;

    reg pending;
    reg [31:0] request_addr;

    // Score and box values use separate line caches to avoid score-first
    // reads evicting the box line for each grid cell.
    reg [1:0] cache_valid;
    reg [31:0] cache_addr[0:1];
    reg [511:0] cache_data[0:1];
    reg [31:0] values[0:4];

    reg [12:0] grid_index;
    reg [2:0] field;
    reg [3:0] state;

    // Address generation is pipelined; its inputs remain stable until lookup.
    reg [12:0] address_cell;
    reg [31:0] address_stride;
    reg [31:0] address_base;
    reg [33:0] address_channel;
    reg [44:0] address_product;
    reg [63:0] address_partial;
    reg [63:0] address_value;
    wire abort_request = wr_valid && (wr_addr == 8) && wr_data[2];

    wire c_busy;
    wire c_done;
    wire c_error;
    wire c_in_ready;
    wire c_out_valid;
    wire [31:0] c_frame;
    wire [7:0] c_buffer;
    wire [12:0] c_candidates;
    wire [12:0] c_index;
    wire [8:0] c_results;
    wire [15:0] c_class;
    wire [31:0] c_score;
    wire [31:0] c_x;
    wire [31:0] c_y;
    wire [31:0] c_w;
    wire [31:0] c_h;

    (* ram_style = "distributed" *) reg [223:0] records[0:31];
    reg [8:0] stored;
    wire pack_ready;
    wire dma_busy;
    wire dma_done;
    wire dma_error;
    wire result_accept = c_out_valid && pack_ready && (stored < 32) &&
                         !failed && !abort_request && (terminal_hold == 0);

    reg [223:0] read_record;
    reg [2:0] read_field;
    reg read_pending;
    reg read_ready;
    reg [31:0] read_result;

    wire [2:0] head = grid_index < 3520 ? 0 : grid_index < 4400 ? 1 : 2;
    wire [12:0] local_cell = grid_index < 3520 ? grid_index :
                              grid_index < 4400 ? grid_index - 13'd3520 :
                              grid_index - 13'd4400;
    wire [4:0] di = (head * 2 + (field != 0)) * 4;
    wire [1:0] channel_number = field[1:0] - 2'd1;
    reg [33:0] channel_offset;
    always @* begin
        channel_offset = 0;
        if (field != 0) begin
            case (channel_number)
                0: channel_offset = 0;
                1: channel_offset = {2'b0, jd[di+3]};
                2: channel_offset = {1'b0, jd[di+3], 1'b0};
                3: channel_offset = {2'b0, jd[di+3]} + {1'b0, jd[di+3], 1'b0};
            endcase
        end
    end

    wire [31:0] line_address = {address_value[31:6], 6'b0};

    assign mem_valid = diag_busy ? diag_send :
                       busy && core_started && !failed && !pending && (state == 1) &&
                       !abort_request && (terminal_hold == 0);
    assign mem_addr = diag_busy ? diag_addr : request_addr;
    assign mem_rsp_ready = diag_busy ? !diag_send : pending;

    reg config_ok;
    reg [63:0] pool_end;
    integer i;

    always @* begin
        config_ok = cfg[8] == 32'h584a0002 && cfg[1] <= 255 && cfg[2] <= 32 &&
                    cfg[4][5:0] == 0 && cfg[5][5:0] == 0 && cfg[5] >= 64 &&
                    cfg[6][0] && cfg[7][0] && (cfg[7] & ~cfg[6]) == 0 &&
                    !confidence[31] && confidence <= 32'h3f800000 &&
                    !cfg[3][31] && cfg[3] <= 32'h3f800000 &&
                    result_base[5:0] == 0 && result_capacity >= 392 &&
                    timeout_cycles != 0 &&
                    64'(result_base) + 64'(result_capacity) <= 64'h40000000;
        pool_end = 64'(cfg[4]) + 64'(cfg[5]);
        if (pool_end > 64'h100000000) config_ok = 0;
    end

    yolo26_postprocess core (
        .clk(clk),
        .resetn(resetn && busy && core_started && (terminal_hold == 0) &&
                !(failed && (reset_lease >= 6))),
        .start(core_start),
        .abort((abort_request || failed) && (reset_lease >= 6)),
        .cfg_frame_id(job[0]),
        .cfg_buffer_index(job[1][7:0]),
        .cfg_limit(job[2][8:0]),
        .cfg_iou(job[3]),
        .cfg_confidence(job_confidence),
        .busy(c_busy),
        .done(c_done),
        .error(c_error),
        .frame_id(c_frame),
        .buffer_index(c_buffer),
        .candidate_count(c_candidates),
        .result_count(c_results),
        .in_valid(busy && core_started && !failed && (state == 2) &&
                  !abort_request && (terminal_hold == 0)),
        .in_ready(c_in_ready),
        .in_logit(values[0]),
        .in_l(values[1]),
        .in_t(values[2]),
        .in_r(values[3]),
        .in_b(values[4]),
        .in_last(grid_index == 4619),
        .out_valid(c_out_valid),
        .out_ready(pack_ready && (stored < 32) && !failed && !abort_request &&
                   (terminal_hold == 0)),
        .out_class(c_class),
        .out_index(c_index),
        .out_score(c_score),
        .out_x(c_x),
        .out_y(c_y),
        .out_w(c_w),
        .out_h(c_h)
    );

    xj_post_result_dma result_writer (
        .clk(clk),
        .resetn(resetn),
        .start(core_start),
        .abort(failed || abort_request),
        .base(job_result_base),
        .frame(job[0]),
        .finish(c_done && !c_error),
        .in_valid(result_accept),
        .in_ready(pack_ready),
        .in_class(c_class),
        .in_score(c_score),
        .in_x(c_x),
        .in_y(c_y),
        .in_w(c_w),
        .in_h(c_h),
        .busy(dma_busy),
        .done(dma_done),
        .error(dma_error),
        .req_valid(ps_valid),
        .req_ready(ps_ready),
        .req_addr(ps_addr),
        .req_data(ps_data),
        .rsp_valid(ps_rsp_valid),
        .rsp_ready(ps_rsp_ready),
        .rsp_error(ps_rsp_error)
    );

    always @* begin
        rd_data = 0;
        case (rd_addr)
            0: rd_data = 32'h584a5002;
            4:
            rd_data = {
                16'b0, reason, 3'b0, (resetn && !busy && !diag_busy), rejected, error, done, busy
            };
            12'h030: rd_data = last_info;
            12'h034: rd_data = error_addr;
            12'h038: rd_data = {19'b0, committed_candidates};
            12'h03c: rd_data = done && !error ? {23'b0, stored} : 0;
            12'h044: rd_data = read_ready ? read_result : 0;
            12'h048: rd_data = {31'b0, read_ready};
            12'h04c: rd_data = confidence;
            12'h050: rd_data = job[0];
            12'h054: rd_data = job[1];
            12'h060: rd_data = {29'b0, diag_error, diag_done, diag_busy};
            12'h064: rd_data = diag_addr;
            12'h06c: rd_data = diag_info;
            12'h0e0: rd_data = result_base;
            12'h0e4: rd_data = result_capacity;
            12'h0e8: rd_data = timeout_cycles;
            12'h0ec: rd_data = 32'h00010020;
            12'h0f0: rd_data = elapsed;
            default: begin
                if ((rd_addr >= 12'h00c) && (rd_addr <= 12'h02c) && (rd_addr[1:0] == 0))
                    rd_data = cfg[(rd_addr-12'h00c)>>2];
                if ((rd_addr >= 12'h080) && (rd_addr < 12'h0e0) && (rd_addr[1:0] == 0))
                    rd_data = desc[(rd_addr-12'h080)>>2];
                if ((rd_addr >= 12'h100) && (rd_addr < 12'h140) && (rd_addr[1:0] == 0))
                    rd_data = diag_done && !diag_error ? diag_data >> ((rd_addr - 12'h100) * 8) : 0;
            end
        endcase
    end
    always @(posedge clk) begin
        if (!resetn) begin
            busy                 <= 0;
            done                 <= 0;
            error                <= 0;
            failed               <= 0;
            rejected             <= 0;
            core_start           <= 0;
            reason               <= 0;
            last_info            <= 0;
            error_addr           <= 0;
            pending              <= 0;
            cache_valid          <= 0;
            request_addr         <= 0;
            cache_addr[0]        <= 0;
            cache_addr[1]        <= 0;
            cache_data[0]        <= 0;
            cache_data[1]        <= 0;
            grid_index           <= 0;
            field                <= 0;
            state                <= 0;
            stored               <= 0;
            read_record          <= 0;
            read_field           <= 0;
            read_pending         <= 0;
            read_ready           <= 0;
            read_result          <= 0;
            address_cell         <= 0;
            address_stride       <= 0;
            address_base         <= 0;
            address_channel      <= 0;
            address_product      <= 0;
            address_partial      <= 0;
            address_value        <= 0;
            reset_lease          <= 0;
            terminal_hold        <= 0;
            finish_error         <= 0;
            committed_candidates <= 0;
            validating           <= 0;
            core_started         <= 0;
            validate_index       <= 0;
            validate_state       <= 0;
            validate_base        <= 0;
            validate_extent      <= 0;
            validate_last        <= 0;
            validate_product     <= 0;
            validate_extra       <= 0;
            validate_bad         <= 0;
            diag_busy            <= 0;
            diag_done            <= 0;
            diag_error           <= 0;
            diag_send            <= 0;
            diag_addr            <= 0;
            diag_info            <= 0;
            diag_data            <= 0;
            confidence           <= 32'h3f000000;
            job_confidence       <= 32'h3f000000;
            result_base          <= 0;
            result_capacity      <= 0;
            job_result_base      <= 0;
            timeout_cycles       <= 100000000;
            job_timeout          <= 100000000;
            elapsed              <= 0;

            for (i = 0; i < 9; i = i + 1) begin
                cfg[i] <= 0;
                job[i] <= 0;
            end
            cfg[2] <= 32;
            cfg[3] <= 32'h3f19999a;

            for (i = 0; i < 24; i = i + 1) begin
                desc[i] <= 0;
                jd[i]   <= 0;
            end
            for (i = 0; i < 5; i = i + 1) values[i] <= 0;
        end else begin
            core_start <= 0;

            if (read_pending) begin
                read_result  <= read_record >> (read_field * 32);
                read_pending <= 0;
                read_ready   <= 1;
            end

            if (wr_valid) begin
                if (wr_addr == 12'h064) begin
                    if (!busy && !diag_busy) diag_addr <= wr_data;
                    else rejected <= 1;
                end

                if ((wr_addr == 12'h068) && wr_data[0]) begin
                    if (busy || diag_busy) rejected <= 1;
                    else begin
                        diag_done  <= 0;
                        diag_error <= 0;
                        diag_info  <= 0;

                        // Accept a diagnostic read only when its full cache line is in range.
                        if ((diag_addr[5:0] != 0) || (cfg[4][5:0] != 0) || (cfg[5] < 64) ||
                            (64'(diag_addr) < 64'(cfg[4])) ||
                            (64'(diag_addr) + 63 >= 64'(cfg[4]) + 64'(cfg[5])) ||
                            (64'(cfg[4]) + 64'(cfg[5]) > 64'h100000000)) begin
                            diag_done  <= 1;
                            diag_error <= 1;
                        end else begin
                            diag_busy <= 1;
                            diag_send <= 1;
                        end
                    end
                end

                if ((wr_addr >= 12'h00c) && (wr_addr <= 12'h02c) && (wr_addr[1:0] == 0))
                    cfg[(wr_addr-12'h00c)>>2] <= wr_data;
                if ((wr_addr >= 12'h080) && (wr_addr < 12'h0e0) && (wr_addr[1:0] == 0))
                    desc[(wr_addr-12'h080)>>2] <= wr_data;
                if (wr_addr == 12'h04c) confidence <= wr_data;
                if (wr_addr == 12'h0e0) result_base <= wr_data;
                if (wr_addr == 12'h0e4) result_capacity <= wr_data;
                if (wr_addr == 12'h0e8) timeout_cycles <= wr_data;

                if (wr_addr == 12'h040) begin
                    read_ready <= 0;
                    if (done && !error && (wr_data[8:0] < stored) && (wr_data[11:9] < 7) &&
                        (wr_data[31:12] == 0) && !read_pending) begin
                        read_record  <= records[wr_data[8:0]];
                        read_field   <= wr_data[11:9];
                        read_pending <= 1;
                    end else begin
                        rejected <= 1;
                    end
                end

                if (wr_addr == 8) begin
                    if (wr_data[1] && !busy) begin
                        done       <= 0;
                        error      <= 0;
                        rejected   <= 0;
                        reason     <= 0;
                        read_ready <= 0;
                    end

                    if (wr_data[0]) begin
                        if (busy || diag_busy) rejected <= 1;
                        else begin
                            done                 <= 0;
                            error                <= 0;
                            failed               <= 0;
                            rejected             <= 0;
                            reason               <= 0;
                            last_info            <= 0;
                            error_addr           <= 0;
                            cache_valid          <= 0;
                            grid_index           <= 0;
                            field                <= 0;
                            state                <= 0;
                            stored               <= 0;
                            read_ready           <= 0;
                            read_pending         <= 0;
                            reset_lease          <= 0;
                            terminal_hold        <= 0;
                            finish_error         <= 0;
                            committed_candidates <= 0;
                            elapsed              <= 0;
                            validating           <= 0;
                            core_started         <= 0;
                            validate_index       <= 0;
                            validate_state       <= 0;

                            if (!config_ok) begin
                                done   <= 1;
                                error  <= 1;
                                reason <= 1;
                            end else begin
                                for (i = 0; i < 9; i = i + 1) job[i] <= cfg[i];
                                for (i = 0; i < 24; i = i + 1) jd[i] <= desc[i];

                                job_confidence  <= confidence;
                                job_result_base <= result_base;
                                job_timeout     <= timeout_cycles;
                                busy            <= 1;
                                validating      <= 1;
                            end
                        end
                    end
                end
            end

            if (diag_busy) begin
                if (diag_send && mem_ready) diag_send <= 0;
                if (mem_rsp_valid && mem_rsp_ready) begin
                    diag_busy  <= 0;
                    diag_done  <= 1;
                    diag_error <= mem_rsp_error;
                    diag_info  <= mem_rsp_info;
                    diag_data  <= mem_rsp_data;
                end
            end

            if (busy) begin
                if (!failed && (terminal_hold == 0)) begin
                    if (elapsed >= job_timeout - 1) begin
                        failed <= 1;
                        reason <= 5;
                    end else begin
                        elapsed <= elapsed + 1'b1;
                    end
                end

                if (core_started && (reset_lease < 8)) reset_lease <= reset_lease + 1'b1;
                if (abort_request) begin
                    failed <= 1;
                    reason <= 4;
                end

                // Validate all six descriptor ranges before starting Decode/NMS.
                if (validating && !failed && !abort_request && (terminal_hold == 0)) begin
                    case (validate_state)
                        0: begin
                            validate_base <= {32'b0, jd[vi]};
                            validate_extent <= {32'b0, jd[vi+1]};
                            validate_product <= jd[vi+2] * tail_count;
                            validate_extra <= validate_index[0] ?
                                              {2'b0, jd[vi+3]} + {1'b0, jd[vi+3], 1'b0} : 34'b0;
                            validate_bad <= jd[vi][1:0] != 0 || jd[vi+2][1:0] != 0 ||
                                            jd[vi+2] < 4 || jd[vi+1] < 4 ||
                                            (validate_index[0] &&
                                             (jd[vi+3][1:0] != 0 || jd[vi+3] < 4)) ||
                                            64'(jd[vi]) < 64'(job[4]);
                            validate_state <= 1;
                        end
                        1: begin
                            validate_last <= validate_base + {20'b0, validate_product} +
                                             {30'b0, validate_extra} + 3;
                            validate_state <= 2;
                        end
                        2: begin
                            if (validate_bad ||
                                (validate_last >= validate_base + validate_extent) ||
                                ((validate_last | 64'd63) >= 64'(job[4]) + 64'(job[5]))) begin
                                failed <= 1;
                                reason <= 1;
                            end else if (validate_index == 5) begin
                                validating   <= 0;
                                core_started <= 1;
                                core_start   <= 1;
                            end else begin
                                validate_index <= validate_index + 1'b1;
                                validate_state <= 0;
                            end
                        end
                    endcase
                end

                if (mem_valid && mem_ready) begin
                    pending <= 1;
                    state   <= 3;
                end
                if (mem_rsp_valid && mem_rsp_ready) begin
                    pending   <= 0;
                    last_info <= mem_rsp_info;
                    if (mem_rsp_error || ((mem_rsp_info & job[6]) != job[7])) begin
                        failed     <= 1;
                        reason     <= 2;
                        error_addr <= request_addr;
                    end else begin
                        cache_valid[field!=0] <= 1;
                        cache_addr[field!=0]  <= request_addr;
                        cache_data[field!=0]  <= mem_rsp_data;
                        state                 <= 0;
                    end
                end

                if (core_started && !failed && !abort_request && (terminal_hold == 0)) begin
                    case (state)
                        0: begin
                            address_cell    <= local_cell;
                            address_stride  <= jd[di+2];
                            address_base    <= jd[di];
                            address_channel <= channel_offset;
                            if (field >= 2) state <= 6;
                            else state <= 4;
                        end
                        4: begin
                            address_product <= address_cell * address_stride;
                            state           <= 5;
                        end
                        5: begin
                            address_partial <= {32'b0, address_base} + {19'b0, address_product};
                            state           <= 6;
                        end
                        6: begin
                            address_value <= address_partial + {30'b0, address_channel};
                            state         <= 7;
                        end
                        7: begin
                            if (cache_valid[field != 0] &&
                                (cache_addr[field != 0] == line_address)) begin
                                values[field] <= cache_data[field!=0] >> (address_value[5:0] * 8);
                                if (field == 4) begin
                                    state <= 2;
                                end else begin
                                    field <= field + 1'b1;
                                    state <= 0;
                                end
                            end else begin
                                request_addr <= line_address;
                                state        <= 1;
                            end
                        end
                        2: begin
                            if (c_in_ready) begin
                                if (grid_index != 4619) begin
                                    grid_index <= grid_index + 1'b1;
                                    field      <= 0;
                                    state      <= 0;
                                end else begin
                                    state <= 3;
                                end
                            end
                        end
                    endcase
                end

                if (result_accept) begin
                    records[stored] <= {
                        c_h, c_w, c_y, c_x, c_score, 19'b0, c_index, 16'b0, c_class
                    };
                    stored <= stored + 1'b1;
                end

                if (c_done) begin
                    if (c_error) begin
                        failed <= 1;
                        reason <= 3;
                    end else if (!pending && !failed && !abort_request && (terminal_hold == 0)) begin
                        committed_candidates <= c_candidates;
                        if (stored != c_results) begin
                            failed <= 1;
                            reason <= 6;
                        end
                    end
                end

                if (dma_done && (terminal_hold == 0) && !failed && !abort_request) begin
                    if (dma_error) begin
                        failed <= 1;
                        reason <= 7;
                    end else begin
                        terminal_hold <= 1;
                    end
                end

                if (failed && !pending && !dma_busy && (!core_started || (reset_lease >= 6)) &&
                    (terminal_hold == 0)) begin
                    terminal_hold <= 1;
                    cache_valid   <= 0;
                    read_ready    <= 0;
                end

                // Hold reset for the minimum interval required by the FP IP.
                if (terminal_hold != 0) begin
                    if (terminal_hold == 3) begin
                        busy  <= 0;
                        done  <= 1;
                        error <= failed || finish_error;
                    end else begin
                        terminal_hold <= terminal_hold + 1'b1;
                    end
                end
            end
        end
    end
endmodule

