// Original FP32 box + padding operations, fixed 1920x1080 source. No tracking.
// Finite intermediates outside int32 conversion range are explicitly rejected.
module cp_roi (
    input wire clk,
    resetn,
    start,
    input wire [31:0] x,
    y,
    w,
    h,
    padding,
    output reg done,
    error,
    output reg [11:0] left,
    top,
    width,
    height
);
    reg [2:0] state;
    reg [3:0] step;
    reg issued;
    reg [31:0] xx, yy, ww, hh, pad, dx, dy, right, bottom, fl, ft, fr, fb;
    reg [31:0] a, b;
    reg sub;
    wire launch = state == 1 && !issued;
    wire use_mul = step == 2 || step == 3;
    wire av, mv;
    wire [31:0] ar, mr;
    wire rv = use_mul ? mv : av;
    wire [31:0] rr = use_mul ? mr : ar;
    function automatic finite(input [31:0] v);
        finite = v[30:23] != 255;
    endfunction
    function automatic positive(input [31:0] v);
        positive = !v[31] && v[30:0] != 0;
    endfunction
    function automatic [33:0] integer_parts(input [31:0] v);
        reg [31:0] mag;
        integer ex;
        reg fractional;
        begin
            ex = v[30:23] - 127;
            mag = {1'b1, v[22:0]};
            fractional = 0;
            if (v[30:0] == 0) mag = 0;
            else if (ex < 0) begin
                mag = 0;
                fractional = 1;
            end else if (ex >= 23) mag = mag << (ex - 23);
            else begin
                fractional = (mag & (32'h007fffff >> ex)) != 0;
                mag = mag >> (23 - ex);
            end
            integer_parts = {v[31], fractional, mag};
        end
    endfunction
    function automatic integer bound(input integer v, l, r);
        bound = v < l ? l : v > r ? r : v;
    endfunction
    integer floor_l, floor_t, ceil_r, ceil_b;
    reg [11:0] l, t, r, bottom_i;
    reg [33:0] parts_l, parts_t, parts_r, parts_b;
    always @* begin
        a   = xx;
        b   = ww;
        sub = 0;
        case (step)
            0: begin
                a = xx;
                b = ww;
            end
            1: begin
                a = yy;
                b = hh;
            end
            2: begin
                a = ww;
                b = pad;
            end
            3: begin
                a = hh;
                b = pad;
            end
            4: begin
                a   = xx;
                b   = dx;
                sub = 1;
            end
            5: begin
                a   = yy;
                b   = dy;
                sub = 1;
            end
            6: begin
                a = right;
                b = dx;
            end
            7: begin
                a = bottom;
                b = dy;
            end
        endcase
    end
    cp_add add_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && !use_mul),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && !use_mul),
        .s_axis_b_tdata(b),
        .s_axis_operation_tvalid(launch && !use_mul),
        .s_axis_operation_tdata({7'b0, sub}),
        .m_axis_result_tvalid(av),
        .m_axis_result_tdata(ar)
    );
    cp_mul mul_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_mul),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && use_mul),
        .s_axis_b_tdata(b),
        .m_axis_result_tvalid(mv),
        .m_axis_result_tdata(mr)
    );
    always @(posedge clk)
        if (!resetn) begin
            state <= 0;
            done <= 0;
            error <= 0;
            step <= 0;
            issued <= 0;
            left <= 0;
            top <= 0;
            width <= 0;
            height <= 0;
        end else begin
            done <= 0;
            case (state)
                0:
                if (start) begin
                    error <= 0;
                    if (!finite(
                            x
                        ) || !finite(
                            y
                        ) || !finite(
                            w
                        ) || !finite(
                            h
                        ) || !finite(
                            padding
                        ) || !positive(
                            w
                        ) || !positive(
                            h
                        ) || (padding[31] && padding[30:0] != 0) || (!x[31] && x >= 32'h44f00000) ||
                            (!y[31] && y >= 32'h44870000)) begin
                        error <= 1;
                        done  <= 1;
                    end else begin
                        xx <= x;
                        yy <= y;
                        ww <= w;
                        hh <= h;
                        pad <= padding;
                        step <= 0;
                        issued <= 0;
                        state <= 1;
                    end
                end
                1: begin
                    if (launch) issued <= 1;
                    if (rv) begin
                        issued <= 0;
                        if (!finite(
                                rr
                            ) || (step <= 1 && !positive(
                                rr
                            )) || (step >= 4 && rr[30:23] >= 158)) begin
                            error <= 1;
                            done  <= 1;
                            state <= 0;
                        end else begin
                            case (step)
                                0: right <= rr;
                                1: bottom <= rr;
                                2: dx <= rr;
                                3: dy <= rr;
                                4: fl <= rr;
                                5: ft <= rr;
                                6: fr <= rr;
                                7: fb <= rr;
                            endcase
                            if (step == 7) state <= 2;
                            else step <= step + 1;
                        end
                    end
                end
                2: begin
                    parts_l <= integer_parts(fl);
                    parts_t <= integer_parts(ft);
                    parts_r <= integer_parts(fr);
                    parts_b <= integer_parts(fb);
                    state   <= 3;
                end
                3: begin
                    floor_l <= parts_l[33] ? (~parts_l[31:0] + !parts_l[32]) : parts_l[31:0];
                    floor_t <= parts_t[33] ? (~parts_t[31:0] + !parts_t[32]) : parts_t[31:0];
                    ceil_r  <= parts_r[33] ? -parts_r[31:0] : parts_r[31:0] + parts_r[32];
                    ceil_b  <= parts_b[33] ? -parts_b[31:0] : parts_b[31:0] + parts_b[32];
                    state   <= 4;
                end
                4: begin
                    l <= bound(floor_l, 0, 1919);
                    t <= bound(floor_t, 0, 1079);
                    state <= 5;
                end
                5: begin
                    r <= bound(ceil_r, l + 1, 1920);
                    bottom_i <= bound(ceil_b, t + 1, 1080);
                    state <= 6;
                end
                6: begin
                    left <= l;
                    top <= t;
                    width <= r - l;
                    height <= bottom_i - t;
                    done <= 1;
                    state <= 0;
                end
            endcase
        end
endmodule
