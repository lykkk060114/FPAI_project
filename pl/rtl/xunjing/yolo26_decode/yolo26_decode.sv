// Independent logical decoder. Six tensors are paired externally into one tuple
// per cell, in level/y/x order. This interface is NOT a device DDR interface.
// FP32 arithmetic uses isolated Vivado Floating Point 7.1 cores. Candidate order
// is traversal order; stable sorting/NMS are subsequent work. No pre-NMS TopK.
module yolo26_decode (
    input wire clk,
    resetn,
    start,
    input wire [31:0] cfg_confidence,
    output reg busy,
    done,
    error,
    input wire in_valid,
    output wire in_ready,
    input wire [31:0] in_logit,
    in_l,
    in_t,
    in_r,
    in_b,
    input wire in_last,
    output reg out_valid,
    input wire out_ready,
    output reg [12:0] out_index,
    output reg [31:0] out_score,
    out_x,
    out_y,
    out_w,
    out_h
);
    reg [1:0] state;
    localparam IDLE = 0, INPUT_CELL = 1, CALC = 2, OUTPUT_CELL = 3;
    reg [4:0] step;
    reg issued, last_cell;
    reg [12:0] index;
    reg [6:0] gx;
    reg [5:0] gy;
    reg [1:0] level;
    reg [31:0] logit, l, t, r, b, temp, score, left, top, right, bottom, width, height;
    reg [31:0] a, bb;
    reg op_sub;
    reg [31:0] confidence;
    wire [31:0] add_result, mul_result, exp_result, div_result;
    wire add_valid, mul_valid, exp_valid, div_valid;
    wire launch = state == CALC && !issued && step < 13;
    wire use_exp = step == 0, use_div = step == 2;
    wire use_mul = step == 4 || step == 6 || step == 8 || step == 10;
    wire use_add = !use_exp && !use_div && !use_mul;
    wire result_valid = use_exp ? exp_valid : use_div ? div_valid : use_mul ? mul_valid : add_valid;
    wire [31:0] result_data=use_exp ? exp_result : use_div ? div_result : use_mul ? mul_result : add_result;
    wire [31:0] stride = level == 0 ? 32'h41000000 : level == 1 ? 32'h41800000 : 32'h42000000;
    // Exact conversion of small integer + 0.5, with no real/shortreal in RTL.
    function automatic [31:0] center(input [6:0] coordinate);
        reg [7:0] twice;
        integer bitno, k;
        reg [31:0] mantissa;
        begin
            twice = {coordinate, 1'b1};
            bitno = 0;
            for (k = 0; k < 8; k = k + 1) if (twice[k]) bitno = k;
            mantissa = twice;
            mantissa = mantissa << (23 - bitno);
            center   = ((126 + bitno) << 23) | (mantissa & 32'h007fffff);
        end
    endfunction
    function automatic finite(input [31:0] value);
        finite = value[30:23] != 8'hff;
    endfunction
    // Positive finite binary32 order coincides with unsigned integer order.
    function automatic at_least_one(input [31:0] value);
        at_least_one = finite(value) && !value[31] && value >= 32'h3f800000;
    endfunction
    wire [31:0] cx = center(gx), cy = center({1'b0, gy});
    assign in_ready = state == INPUT_CELL;
    always @* begin
        a = temp;
        bb = 32'h3f800000;
        op_sub = 0;
        case (step)
            0: a = {~logit[31], logit[30:0]};
            1: begin
                a  = temp;
                bb = 32'h3f800000;
            end
            2: begin
                a  = 32'h3f800000;
                bb = temp;
            end
            3: begin
                a = cx;
                bb = l;
                op_sub = 1;
            end
            5: begin
                a = cy;
                bb = t;
                op_sub = 1;
            end
            7: begin
                a  = cx;
                bb = r;
            end
            9: begin
                a  = cy;
                bb = b;
            end
            4, 6, 8, 10: begin
                a  = temp;
                bb = stride;
            end
            11: begin
                a = right;
                bb = left;
                op_sub = 1;
            end
            12: begin
                a = bottom;
                bb = top;
                op_sub = 1;
            end
        endcase
    end
    yd_add add_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_add),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && use_add),
        .s_axis_b_tdata(bb),
        .s_axis_operation_tvalid(launch && use_add),
        .s_axis_operation_tdata({7'b0, op_sub}),
        .m_axis_result_tvalid(add_valid),
        .m_axis_result_tdata(add_result)
    );
    yd_mul mul_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_mul),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && use_mul),
        .s_axis_b_tdata(bb),
        .m_axis_result_tvalid(mul_valid),
        .m_axis_result_tdata(mul_result)
    );
    yd_exp exp_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_exp),
        .s_axis_a_tdata(a),
        .m_axis_result_tvalid(exp_valid),
        .m_axis_result_tdata(exp_result)
    );
    yd_div div_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_div),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && use_div),
        .s_axis_b_tdata(bb),
        .m_axis_result_tvalid(div_valid),
        .m_axis_result_tdata(div_result)
    );
    task automatic advance_cell;
        begin
            if (last_cell) begin
                busy  <= 0;
                done  <= 1;
                state <= IDLE;
            end else begin
                index <= index + 1;
                state <= INPUT_CELL;
                if (gx == (level == 0 ? 79 : level == 1 ? 39 : 19)) begin
                    gx <= 0;
                    if (gy == (level == 0 ? 43 : level == 1 ? 21 : 10)) begin
                        gy <= 0;
                        level <= level + 1;
                    end else gy <= gy + 1;
                end else gx <= gx + 1;
            end
        end
    endtask
    always @(posedge clk) begin
        if (!resetn) begin
            state <= IDLE;
            busy <= 0;
            done <= 0;
            error <= 0;
            out_valid <= 0;
            index <= 0;
            gx <= 0;
            gy <= 0;
            level <= 0;
            step <= 0;
            issued <= 0;
            out_index <= 0;
            out_score <= 0;
            out_x <= 0;
            out_y <= 0;
            out_w <= 0;
            out_h <= 0;
            confidence <= 32'h3f000000;
        end else begin
            done <= 0;
            case (state)
                IDLE:
                if (start) begin
                    busy <= 1;
                    error <= 0;
                    index <= 0;
                    gx <= 0;
                    gy <= 0;
                    level <= 0;
                    out_valid <= 0;
                    confidence <= cfg_confidence;
                    state <= INPUT_CELL;
                end
                INPUT_CELL:
                if (in_valid) begin
                    if (in_last != (index == 4619)) begin
                        error <= 1;
                        busy  <= 0;
                        state <= IDLE;
                    end else begin
                        // std::clamp preserves NaN and clamps both infinities.
                        logit<=in_logit[30:0]>31'h42a00000 && !(in_logit[30:0]>31'h7f800000)
                             ? {in_logit[31],31'h42a00000} : in_logit;
                        l <= in_l;
                        t <= in_t;
                        r <= in_r;
                        b <= in_b;
                        last_cell <= in_last;
                        // Safe rejection only: NaN never yields a finite score;
                        // clamped logit <= -1 yields score < 0.5 with a wide margin.
                        // Do NOT reject every negative logit: rounding near zero
                        // can produce exactly 0.5, which the CPU retains.
                        step<=((in_logit[30:0]>31'h7f800000) ||
                             (confidence>=32'h3f000000 && in_logit[31] && in_logit[30:0]>=31'h3f800000)) ? 14 : 0;
                        issued <= 0;
                        state <= CALC;
                    end
                end
                CALC: begin
                    if (step == 14) advance_cell();
                    else if (step == 13) begin
                        if (finite(
                                left
                            ) && finite(
                                top
                            ) && at_least_one(
                                width
                            ) && at_least_one(
                                height
                            )) begin
                            out_index <= index;
                            out_score <= score;
                            out_x <= left;
                            out_y <= top;
                            out_w <= width;
                            out_h <= height;
                            out_valid <= 1;
                            state <= OUTPUT_CELL;
                        end else advance_cell();
                    end else if (!issued) issued <= 1;
                    else if (result_valid) begin
                        issued <= 0;
                        step   <= step + 1;
                        temp   <= result_data;
                        case (step)
                            2: begin
                                score <= result_data;
                                if (!finite(
                                        result_data
                                    ) || result_data[31] || result_data < confidence)
                                    advance_cell();
                            end
                            4:  left <= result_data;
                            6:  top <= result_data;
                            8:  right <= result_data;
                            10: bottom <= result_data;
                            11: width <= result_data;
                            12: height <= result_data;
                        endcase
                    end
                end
                OUTPUT_CELL:
                if (out_ready) begin
                    out_valid <= 0;
                    advance_cell();
                end
            endcase
        end
    end
endmodule
