// Derived from verified NMS v2 SHA256 4595bd872a07bbe4b13178a3c64cfe92defab0cc0918e04a5cf504823687dac2
// Only ingress/count commit differs; selection and FP arithmetic are retained.
// Standalone logical candidate interface, not a device DDR/register contract.
// Stable descending selection of the full surviving set is equivalent to
// CPU stable_sort followed by greedy suppression; there is no pre-NMS TopK.
module yolo26_nms_commit #(
    parameter MAX_CANDIDATES = 4620
) (
    input wire clk,
    resetn,
    start,
    // Count is published only after Decode completed the entire frame.
    input wire commit_valid,
    output wire commit_ready,
    input wire [12:0] commit_count,
    input wire [8:0] cfg_limit,
    input wire [31:0] cfg_iou,
    output reg busy,
    done,
    error,
    input wire in_valid,
    output wire in_ready,
    input wire in_last,
    input wire [15:0] in_class,
    input wire [12:0] in_index,
    input wire [31:0] in_score,
    in_x,
    in_y,
    in_w,
    in_h,
    output reg out_valid,
    input wire out_ready,
    output wire [15:0] out_class,
    output wire [12:0] out_index,
    output wire [31:0] out_score,
    out_x,
    out_y,
    out_w,
    out_h
);
    localparam IDLE=0, LOAD=1, FP=2, STORE=3, SCAN_REQ=4,
               SCAN_EVAL=5, SELECT_BEST=6, EMIT=7, SUP_REQ=8, SUP_EVAL=9, SUP_COMPARE=10;
    reg [3:0] state;
    reg [12:0] count, load_index, scan_index, best_index, sup_index;
    reg [8:0] limit, selected_count;
    reg [31:0] threshold;
    // class/index/score/x/y/w/h/right/bottom/area. No reset on RAM arrays.
    (* ram_style="block" *) reg [284:0] records[0:MAX_CANDIDATES-1];
    (* ram_style="distributed" *) reg alive[0:MAX_CANDIDATES-1];
    reg [284:0] read_record, best_record, selected_record;
    reg read_alive, best_valid;
    reg overlap_mode, issued;
    wire [12:0] read_index=(state==SUP_REQ || state==SUP_EVAL || state==SUP_COMPARE || (state==FP&&overlap_mode)) ? sup_index : scan_index;
    reg [188:0] input_record, output_record;
    reg [31:0] new_right, new_bottom, new_area;
    reg [31:0] edge_l, edge_t, edge_r, edge_b, other_area;
    reg [31:0] iw, ih, intersection, area_sum, total;
    reg [2:0] step;
    reg [31:0] a, b;
    reg subtract;
    wire use_mul = step == 2;
    wire use_div = overlap_mode && step == 5;
    wire use_add = !use_mul && !use_div;
    wire launch = state == FP && !issued;
    wire [31:0] add_data, mul_data, div_data;
    wire add_valid, mul_valid, div_valid;
    wire result_valid = use_mul ? mul_valid : use_div ? div_valid : add_valid;
    wire [31:0] result_data = use_mul ? mul_data : use_div ? div_data : add_data;
    function automatic finite(input [31:0] v);
        finite = v[30:23] != 8'hff;
    endfunction
    function automatic nan(input [31:0] v);
        nan = v[30:23] == 8'hff && v[22:0] != 0;
    endfunction
    function automatic gt(input [31:0] aa, bb);
        begin
            if (nan(aa) || nan(bb) || (aa[30:0] == 0 && bb[30:0] == 0)) gt = 0;
            else if (aa[31] != bb[31]) gt = !aa[31];
            else gt = aa[31] ? aa[30:0] < bb[30:0] : aa[30:0] > bb[30:0];
        end
    endfunction
    function automatic [31:0] fmin(input [31:0] aa, bb);
        fmin = gt(aa, bb) ? bb : aa;
    endfunction
    function automatic [31:0] fmax(input [31:0] aa, bb);
        fmax = gt(bb, aa) ? bb : aa;
    endfunction
    wire [31:0] overlap_l = fmax(selected_record[223:192], read_record[223:192]);
    wire [31:0] overlap_t = fmax(selected_record[191:160], read_record[191:160]);
    wire [31:0] overlap_r = fmin(selected_record[95:64], read_record[95:64]);
    wire [31:0] overlap_b = fmin(selected_record[63:32], read_record[63:32]);
    wire good_input = finite(
        in_score
    ) && !gt(
        32'b0, in_score
    ) && !gt(
        in_score, 32'h3f800000
    ) && finite(
        in_x
    ) && finite(
        in_y
    ) && finite(
        in_w
    ) && finite(
        in_h
    ) && !gt(
        32'h3f800000, in_w
    ) && !gt(
        32'h3f800000, in_h
    );
    assign in_ready = state == LOAD && load_index < MAX_CANDIDATES && !commit_valid;
    assign commit_ready = state == LOAD;
    assign {out_class, out_index, out_score, out_x, out_y, out_w, out_h} = output_record;
    always @(posedge clk) begin
        read_record <= records[read_index];
        read_alive  <= alive[read_index];
        if (state == STORE) begin
            records[load_index] <= {input_record, new_right, new_bottom, new_area};
            alive[load_index]   <= 1;
        end else if (state == SELECT_BEST && best_valid) alive[best_index] <= 0;
        else if (state == FP && overlap_mode && issued && step == 5 && result_valid && gt(
                result_data, threshold
            ))
            alive[sup_index] <= 0;
    end
    always @* begin
        a = 0;
        b = 0;
        subtract = 0;
        if (!overlap_mode) begin
            case (step)
                0: begin
                    a = input_record[127:96];
                    b = input_record[63:32];
                end  // x+w
                1: begin
                    a = input_record[95:64];
                    b = input_record[31:0];
                end  // y+h
                2: begin
                    a = input_record[63:32];
                    b = input_record[31:0];
                end  // w*h
            endcase
        end else begin
            case (step)
                0: begin
                    a = edge_r;
                    b = edge_l;
                    subtract = 1;
                end
                1: begin
                    a = edge_b;
                    b = edge_t;
                    subtract = 1;
                end
                2: begin
                    a = iw;
                    b = ih;
                end
                3: begin
                    a = selected_record[31:0];
                    b = other_area;
                end
                4: begin
                    a = area_sum;
                    b = intersection;
                    subtract = 1;
                end
                5: begin
                    a = intersection;
                    b = total;
                end
            endcase
        end
    end
    yn_add add_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_add),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && use_add),
        .s_axis_b_tdata(b),
        .s_axis_operation_tvalid(launch && use_add),
        .s_axis_operation_tdata({7'b0, subtract}),
        .m_axis_result_tvalid(add_valid),
        .m_axis_result_tdata(add_data)
    );
    yn_mul mul_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_mul),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && use_mul),
        .s_axis_b_tdata(b),
        .m_axis_result_tvalid(mul_valid),
        .m_axis_result_tdata(mul_data)
    );
    yn_div div_core (
        .aclk(clk),
        .aresetn(resetn),
        .s_axis_a_tvalid(launch && use_div),
        .s_axis_a_tdata(a),
        .s_axis_b_tvalid(launch && use_div),
        .s_axis_b_tdata(b),
        .m_axis_result_tvalid(div_valid),
        .m_axis_result_tdata(div_data)
    );
    task automatic next_selection;
        begin
            scan_index <= 0;
            best_valid <= 0;
            state <= SCAN_REQ;
        end
    endtask
    task automatic next_suppression;
        begin
            // The maximum surviving candidate is gathered in this same pass.
            if (sup_index + 1 == count) state <= SELECT_BEST;
            else begin
                sup_index <= sup_index + 1;
                state <= SUP_REQ;
            end
        end
    endtask
    task automatic keep_candidate;
        begin
            // Accepted scores are finite [0,1]; ignoring sign also makes +/-0
            // compare equal. No NaN/general signed comparator is needed here.
            if (!best_valid || read_record[254:224] > best_record[254:224]) begin
                best_valid  <= 1;
                best_index  <= sup_index;
                best_record <= read_record;
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
            count <= 0;
            limit <= 0;
            threshold <= 0;
            load_index <= 0;
            scan_index <= 0;
            sup_index <= 0;
            best_index <= 0;
            best_valid <= 0;
            selected_count <= 0;
            overlap_mode <= 0;
            issued <= 0;
            step <= 0;
            output_record <= 0;
        end else begin
            done <= 0;
            case (state)
                IDLE:
                if (start) begin
                    error <= 0;
                    out_valid <= 0;
                    count <= 0;
                    limit <= cfg_limit;
                    threshold <= cfg_iou;
                    selected_count <= 0;
                    load_index <= 0;
                    if (cfg_limit > 300 || !finite(
                            cfg_iou
                        ) || gt(
                            0, cfg_iou
                        ) || gt(
                            cfg_iou, 32'h3f800000
                        )) begin
                        error <= 1;
                        busy  <= 0;
                    end else begin
                        busy  <= 1;
                        state <= LOAD;
                    end
                end
                LOAD:
                if (commit_valid) begin
                    if (commit_count != load_index || commit_count > MAX_CANDIDATES) begin
                        busy  <= 0;
                        error <= 1;
                        state <= IDLE;
                    end else begin
                        count <= commit_count;
                        if (commit_count == 0 || limit == 0) begin
                            busy  <= 0;
                            done  <= 1;
                            state <= IDLE;
                        end else next_selection();
                    end
                end else if (in_valid) begin
                    // Candidate end-of-frame is a separate commit, never a guessed last.
                    if (load_index >= MAX_CANDIDATES || in_last || !good_input) begin
                        busy  <= 0;
                        error <= 1;
                        state <= IDLE;
                    end else begin
                        input_record <= {in_class, in_index, in_score, in_x, in_y, in_w, in_h};
                        overlap_mode <= 0;
                        step <= 0;
                        issued <= 0;
                        state <= FP;
                    end
                end
                FP:
                if (!issued) issued <= 1;
                else if (result_valid) begin
                    issued <= 0;
                    step   <= step + 1;
                    if (!overlap_mode)
                        case (step)
                            0: new_right <= result_data;
                            1: new_bottom <= result_data;
                            2: begin
                                new_area <= result_data;
                                state <= STORE;
                            end
                        endcase
                    else
                        case (step)
                            0: iw <= result_data;
                            1: ih <= result_data;
                            2: intersection <= result_data;
                            3: area_sum <= result_data;
                            4: begin
                                total <= result_data;
                                if (!gt(result_data, 0)) begin
                                    keep_candidate();
                                    next_suppression();
                                end
                            end
                            5: begin
                                if (!gt(result_data, threshold)) keep_candidate();
                                next_suppression();
                            end
                        endcase
                end
                STORE: begin
                    load_index <= load_index + 1;
                    state <= LOAD;
                end
                SCAN_REQ: state <= SCAN_EVAL;
                SCAN_EVAL: begin
                    if(read_alive&&(!best_valid||read_record[254:224]>best_record[254:224])) begin
                        best_valid  <= 1;
                        best_index  <= scan_index;
                        best_record <= read_record;
                    end
                    if (scan_index + 1 == count) state <= SELECT_BEST;
                    else begin
                        scan_index <= scan_index + 1;
                        state <= SCAN_REQ;
                    end
                end
                SELECT_BEST:
                if (!best_valid) begin
                    busy  <= 0;
                    done  <= 1;
                    state <= IDLE;
                end else begin
                    selected_record <= best_record;
                    output_record <= best_record[284:96];
                    out_valid <= 1;
                    state <= EMIT;
                end
                EMIT:
                if (out_ready) begin
                    out_valid <= 0;
                    selected_count <= selected_count + 1;
                    if (selected_count + 1 == limit) begin
                        busy  <= 0;
                        done  <= 1;
                        state <= IDLE;
                    end else begin
                        sup_index <= 0;
                        best_valid <= 0;
                        state <= SUP_REQ;
                    end
                end
                SUP_REQ:  state <= SUP_EVAL;
                SUP_EVAL:
                if (!read_alive) next_suppression();
                else if (read_record[284:269] != selected_record[284:269]) begin
                    keep_candidate();
                    next_suppression();
                end else begin
                    edge_l <= overlap_l;
                    edge_t <= overlap_t;
                    edge_r <= overlap_r;
                    edge_b <= overlap_b;
                    other_area <= read_record[31:0];
                    state <= SUP_COMPARE;
                end
                SUP_COMPARE:
                if (!gt(edge_r, edge_l) || !gt(edge_b, edge_t)) begin
                    keep_candidate();
                    next_suppression();
                end else begin
                    overlap_mode <= 1;
                    step <= 0;
                    issued <= 0;
                    state <= FP;
                end
            endcase
        end
    end
endmodule
