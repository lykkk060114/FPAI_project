// Logical whole-frame Decode -> NMS, no DDR address or PS register assumptions.
// Exactly one candidate RAM lives in yolo26_nms_commit. No staging/TopK RAM.
module yolo26_postprocess (
    input wire clk,
    resetn,
    start,
    abort,
    input wire [31:0] cfg_frame_id,
    input wire [7:0] cfg_buffer_index,
    input wire [8:0] cfg_limit,
    input wire [31:0] cfg_iou,
    cfg_confidence,
    output reg busy,
    done,
    error,
    output reg [31:0] frame_id,
    output reg [7:0] buffer_index,
    output reg [12:0] candidate_count,
    output reg [8:0] result_count,
    input wire in_valid,
    output wire in_ready,
    input wire [31:0] in_logit,
    in_l,
    in_t,
    in_r,
    in_b,
    input wire in_last,
    output wire out_valid,
    input wire out_ready,
    output wire [15:0] out_class,
    output wire [12:0] out_index,
    output wire [31:0] out_score,
    out_x,
    out_y,
    out_w,
    out_h
);
    localparam IDLE = 0, INIT = 1, LAUNCH = 2, RUN = 3, CANCEL = 4;
    reg [2:0] state;
    reg [8:0] limit;
    reg [31:0] iou, confidence;
    reg commit_pending, commit_sent;
    wire core_resetn = resetn && (state == LAUNCH || state == RUN) && !abort;
    wire core_start = state == LAUNCH && !abort;
    wire d_busy, d_done, d_error, d_ready, d_valid;
    wire [12:0] d_index;
    wire [31:0] d_score, d_x, d_y, d_w, d_h;
    wire n_busy, n_done, n_error, n_ready, n_valid, c_ready;
    wire running = state == RUN && !abort;
    assign in_ready  = running && d_ready;
    assign out_valid = running && n_valid;
    yolo26_decode decode_core (
        .clk(clk),
        .resetn(core_resetn),
        .start(core_start),
        .cfg_confidence(confidence),
        .busy(d_busy),
        .done(d_done),
        .error(d_error),
        .in_valid(in_valid && running),
        .in_ready(d_ready),
        .in_logit(in_logit),
        .in_l(in_l),
        .in_t(in_t),
        .in_r(in_r),
        .in_b(in_b),
        .in_last(in_last),
        .out_valid(d_valid),
        .out_ready(n_ready && running),
        .out_index(d_index),
        .out_score(d_score),
        .out_x(d_x),
        .out_y(d_y),
        .out_w(d_w),
        .out_h(d_h)
    );
    yolo26_nms_commit nms_core (
        .clk(clk),
        .resetn(core_resetn),
        .start(core_start),
        .commit_valid(commit_pending && running),
        .commit_ready(c_ready),
        .commit_count(candidate_count),
        .cfg_limit(limit),
        .cfg_iou(iou),
        .busy(n_busy),
        .done(n_done),
        .error(n_error),
        .in_valid(d_valid && running),
        .in_ready(n_ready),
        .in_last(1'b0),
        .in_class(16'b0),
        .in_index(d_index),
        .in_score(d_score),
        .in_x(d_x),
        .in_y(d_y),
        .in_w(d_w),
        .in_h(d_h),
        .out_valid(n_valid),
        .out_ready(out_ready && running),
        .out_class(out_class),
        .out_index(out_index),
        .out_score(out_score),
        .out_x(out_x),
        .out_y(out_y),
        .out_w(out_w),
        .out_h(out_h)
    );
    always @(posedge clk) begin
        if (!resetn) begin
            state <= IDLE;
            busy <= 0;
            done <= 0;
            error <= 0;
            frame_id <= 0;
            buffer_index <= 0;
            candidate_count <= 0;
            result_count <= 0;
            limit <= 0;
            iou <= 0;
            confidence <= 32'h3f000000;
            commit_pending <= 0;
            commit_sent <= 0;
        end else begin
            done <= 0;
            if (busy && abort && state != CANCEL) begin
                state <= CANCEL;
            end else
                case (state)
                    IDLE:
                    if (start && !abort) begin
                        state <= INIT;
                        busy <= 1;
                        error <= 0;
                        frame_id <= cfg_frame_id;
                        buffer_index <= cfg_buffer_index;
                        limit <= cfg_limit;
                        iou <= cfg_iou;
                        confidence <= cfg_confidence;
                        candidate_count <= 0;
                        result_count <= 0;
                        commit_pending <= 0;
                        commit_sent <= 0;
                        if (cfg_confidence[31] || cfg_confidence > 32'h3f800000) state <= CANCEL;
                    end
                    INIT: state <= LAUNCH;
                    LAUNCH: state <= RUN;
                    RUN: begin
                        if (d_valid && n_ready) candidate_count <= candidate_count + 1'b1;
                        if (out_valid && out_ready) result_count <= result_count + 1'b1;
                        if (d_done) commit_pending <= 1;
                        if (commit_pending && c_ready) begin
                            commit_pending <= 0;
                            commit_sent <= 1;
                        end
                        if (d_error || n_error) state <= CANCEL;
                        else if (n_done && commit_sent) begin
                            busy  <= 0;
                            done  <= 1;
                            state <= IDLE;
                        end
                    end
                    CANCEL: begin
                        busy <= 0;
                        done <= 1;
                        error <= 1;
                        commit_pending <= 0;
                        state <= IDLE;
                    end
                    default: state <= CANCEL;
                endcase
        end
    end
endmodule
