// Writes the result header and fixed-capacity SoA result arrays to PS DDR.
// Completion is reported after the final AXI write response is received.
module xj_post_result_dma (
    input wire        clk,
    input wire        resetn,
    input wire        start,
    input wire        abort,
    input wire [31:0] base,
    input wire [31:0] frame,
    input wire        finish,

    input  wire        in_valid,
    output wire        in_ready,
    input  wire [15:0] in_class,
    input  wire [31:0] in_score,
    input  wire [31:0] in_x,
    input  wire [31:0] in_y,
    input  wire [31:0] in_w,
    input  wire [31:0] in_h,

    output reg busy,
    output reg done,
    output reg error,

    output wire        req_valid,
    input  wire        req_ready,
    output wire [31:0] req_addr,
    output wire [31:0] req_data,
    input  wire        rsp_valid,
    output wire        rsp_ready,
    input  wire        rsp_error
);

    localparam [1:0] COLLECT = 2'd0;
    localparam [1:0] LOAD    = 2'd1;
    localparam [1:0] SEND    = 2'd2;
    localparam [1:0] WAIT_B  = 2'd3;

    reg [1:0] state;
    reg [3:0] pack_step;
    reg [5:0] count;
    reg finished;
    reg cancelled;
    reg [31:0] dst;
    reg [31:0] frame_id;
    reg [31:0] x;
    reg [31:0] y;
    reg [31:0] w;
    reg [31:0] h;
    reg [31:0] score;
    reg [15:0] class_id;
    reg signed [63:0] fx;
    reg signed [63:0] fy;
    reg signed [63:0] fw;
    reg signed [63:0] fh;
    reg signed [63:0] edge_x1;
    reg signed [63:0] edge_x2;
    reg signed [63:0] edge_y1;
    reg signed [63:0] edge_y2;
    reg [15:0] x1;
    reg [15:0] x2;
    reg [15:0] y1;
    reg [15:0] y2;

    (* ram_style = "distributed" *) reg [31:0] words[0:95];
    reg [6:0] word_index;
    reg [31:0] write_addr;
    reg [31:0] write_data;
    reg [31:0] operand;

    always @* begin
        case (pack_step)
            1:       operand = x;
            2:       operand = y;
            3:       operand = w;
            default: operand = h;
        endcase
    end

    function automatic signed [63:0] to_fixed(input [31:0] value);
        reg [63:0] magnitude;
        integer shift;
        begin
            magnitude = {40'b0, 1'b1, value[22:0]};
            shift = $signed({1'b0, value[30:23]}) - 134;
            if (value[30:23] == 0) magnitude = 0;
            else if (shift >= 0) magnitude = magnitude << shift;
            else if (shift <= -64) magnitude = 0;
            else magnitude = magnitude >> (-shift);

            to_fixed = value[31] ? -$signed(magnitude) : $signed(magnitude);
        end
    endfunction

    wire signed [63:0] converted = to_fixed(operand);

    function automatic [15:0] pixel(input signed [63:0] value, input [15:0] maximum);
        reg signed [63:0] upper;
        begin
            upper = $signed({48'b0, maximum}) <<< 16;
            if (value <= 0) pixel = 0;
            else if (value >= upper) pixel = maximum;
            else pixel = (value + 64'sd32768) >>> 16;
        end
    endfunction

    function automatic [15:0] q15(input [31:0] value);
        reg [63:0] magnitude;
        integer shift;
        begin
            magnitude = {40'b0, 1'b1, value[22:0]};
            shift = 135 - value[30:23];
            if (value[30:23] == 0 || shift > 63) q15 = 0;
            else if (value >= 32'h3f800000) q15 = 16'd32768;
            else q15 = (magnitude + (64'd1 << (shift - 1))) >> shift;
        end
    endfunction

    wire invalid_number = (operand[30:23] == 255) || (operand[30:23] > 157);

    assign in_ready = busy && (state == COLLECT) && (pack_step == 0) &&
                      !finished && !cancelled && !abort && (count < 32);
    assign req_valid = busy && (state == SEND) && !abort && !cancelled;
    assign req_addr = write_addr;
    assign req_data = write_data;
    assign rsp_ready = busy && (state == WAIT_B);

    always @(posedge clk) begin
        if (!resetn) begin
            busy       <= 0;
            done       <= 0;
            error      <= 0;
            state      <= COLLECT;
            pack_step  <= 0;
            count      <= 0;
            finished   <= 0;
            cancelled  <= 0;
            dst        <= 0;
            frame_id   <= 0;
            word_index <= 0;
            write_addr <= 0;
            write_data <= 0;
            x          <= 0;
            y          <= 0;
            w          <= 0;
            h          <= 0;
            score      <= 0;
            class_id   <= 0;
            fx         <= 0;
            fy         <= 0;
            fw         <= 0;
            fh         <= 0;
            edge_x1    <= 0;
            edge_x2    <= 0;
            edge_y1    <= 0;
            edge_y2    <= 0;
            x1         <= 0;
            x2         <= 0;
            y1         <= 0;
            y2         <= 0;
        end else begin
            done <= 0;

            if (!busy) begin
                if (start) begin
                    busy       <= 1;
                    error      <= 0;
                    cancelled  <= 0;
                    finished   <= 0;
                    pack_step  <= 0;
                    state      <= COLLECT;
                    count      <= 0;
                    dst        <= base;
                    frame_id   <= frame;
                    word_index <= 0;
                end
            end else begin
                if (finish) finished <= 1;
                if (abort) begin
                    cancelled <= 1;
                    error     <= 1;
                end

                // An accepted write must receive its B response before exit.
                if ((abort || cancelled) && (state != WAIT_B)) begin
                    busy      <= 0;
                    done      <= 1;
                    error     <= 1;
                    pack_step <= 0;
                end else begin
                    case (state)
                        COLLECT: begin
                            if (in_valid && in_ready) begin
                                x         <= in_x;
                                y         <= in_y;
                                w         <= in_w;
                                h         <= in_h;
                                score     <= in_score;
                                class_id  <= in_class;
                                pack_step <= 1;
                            end

                            if ((pack_step >= 1) && (pack_step <= 4) && invalid_number) begin
                                cancelled <= 1;
                                error     <= 1;
                            end

                            case (pack_step)
                                1: begin
                                    fx        <= converted;
                                    pack_step <= 2;
                                end
                                2: begin
                                    fy        <= converted;
                                    pack_step <= 3;
                                end
                                3: begin
                                    fw        <= converted;
                                    pack_step <= 4;
                                end
                                4: begin
                                    fh        <= converted;
                                    pack_step <= 5;
                                end
                                5: begin
                                    edge_x1   <= fx + (fx <<< 1);
                                    edge_x2   <= (fx + fw) + ((fx + fw) <<< 1);
                                    pack_step <= 6;
                                end
                                6: begin
                                    edge_y1   <= fy + (fy <<< 1) + 64'sd786432;
                                    edge_y2   <= (fy + fh) + ((fy + fh) <<< 1) + 64'sd786432;
                                    x1        <= pixel(edge_x1, 1919);
                                    x2        <= pixel(edge_x2, 1919);
                                    pack_step <= 7;
                                end
                                7: begin
                                    y1        <= pixel(edge_y1, 1079);
                                    y2        <= pixel(edge_y2, 1079);
                                    pack_step <= 8;
                                end
                                8: begin
                                    words[{count[4:0], 1'b0}] <= {y1, x1};
                                    pack_step <= 9;
                                end
                                9: begin
                                    words[{count[4:0], 1'b0}+1] <= {y2, x2};
                                    pack_step <= 10;
                                end
                                10: begin
                                    if (score[31] || (score > 32'h3f800000)) begin
                                        cancelled <= 1;
                                        error     <= 1;
                                    end
                                    if (!count[0]) words[64+count[4:1]] <= {16'b0, q15(score)};
                                    else words[64+count[4:1]][31:16] <= q15(score);
                                    pack_step <= 11;
                                end
                                11: begin
                                    if (!count[0]) words[80+count[4:1]] <= {16'b0, class_id};
                                    else words[80+count[4:1]][31:16] <= class_id;
                                    count     <= count + 1'b1;
                                    pack_step <= 0;
                                end
                            endcase

                            if (finished && (pack_step == 0)) begin
                                state      <= LOAD;
                                word_index <= 0;
                            end
                        end

                        LOAD: begin
                            // Clear unused result slots as part of each transfer.
                            write_addr <= dst + 8 + {23'b0, word_index, 2'b0};
                            if (word_index < 64)
                                write_data <= (word_index < ({1'b0, count} << 1)) ? words[word_index] : 0;
                            else if (word_index < 80)
                                write_data <= ((word_index - 64) < (({1'b0, count} + 1) >> 1)) ? words[word_index] : 0;
                            else if (word_index < 96)
                                write_data <= ((word_index - 80) < (({1'b0, count} + 1) >> 1)) ? words[word_index] : 0;
                            else if (word_index == 96) begin
                                write_addr <= dst;
                                write_data <= frame_id;
                            end else begin
                                write_addr <= dst + 4;
                                write_data <= {26'b0, count};
                            end
                            state <= SEND;
                        end

                        SEND: begin
                            if (req_valid && req_ready) state <= WAIT_B;
                        end

                        WAIT_B: begin
                            if (rsp_valid) begin
                                if (rsp_error || abort || cancelled) begin
                                    busy  <= 0;
                                    done  <= 1;
                                    error <= 1;
                                end else if (word_index == 97) begin
                                    busy <= 0;
                                    done <= 1;
                                end else begin
                                    word_index <= word_index + 1'b1;
                                    state      <= LOAD;
                                end
                            end
                        end

                        default: state <= COLLECT;
                    endcase
                end
            end
        end
    end

endmodule
