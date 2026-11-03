// Reproduce original double scale=224/max(w,h), double multiply by short side,
// then std::round (ties away from zero). Integer half-up alone is NOT equivalent.
// All source ROI sizes are bounded to 1920x1080. No generic FP64 IP is needed:
// rounded binary64 mantissa and product are computed exactly with integer RTL.
module cp_dimensions (
    input wire clk,
    resetn,
    start,
    input wire [11:0] src_width,
    src_height,
    output reg busy,
    done,
    error,
    output reg [7:0] resize_width,
    resize_height,
    pad_left,
    pad_top
);
    reg [3:0] state;
    reg [11:0] long_side;
    reg [10:0] short_side;
    reg width_major;
    reg signed [4:0] exponent;
    reg [63:0] numerator;
    reg [15:0] denominator;
    reg div_start;
    wire div_done;
    wire [63:0] quotient;
    wire [16:0] remainder;
    reg [52:0] scale_mantissa;
    reg [28:0] product_low, product_mid;
    reg [27:0] product_high;
    reg [46:0] combined_low;
    reg [63:0] product;
    reg [3:0] drop;
    reg [52:0] head;
    reg increment;
    reg [53:0] rounded_head;
    reg [5:0] integer_shift;
    reg [15:0] raw_dimension;
    integer e, k, b;
    reg [10:0] tail, half;
    integer dim;
    cp_udiv divider (
        clk,
        resetn,
        div_start,
        numerator,
        denominator,
        div_done,
        quotient,
        remainder
    );
    always @(posedge clk)
        if (!resetn) begin
            state <= 0;
            busy <= 0;
            done <= 0;
            error <= 0;
            div_start <= 0;
            resize_width <= 0;
            resize_height <= 0;
            pad_left <= 0;
            pad_top <= 0;
        end else begin
            done <= 0;
            div_start <= 0;
            case (state)
                0:
                if (start) begin
                    error <= 0;
                    if(src_width==0||src_height==0||src_width>1920||src_height>1080)begin
                        done  <= 1;
                        error <= 1;
                    end else begin
                        busy <= 1;
                        width_major <= src_width >= src_height;
                        long_side <= src_width >= src_height ? src_width : src_height;
                        short_side <= src_width >= src_height ? src_height : src_width;
                        state <= 1;
                    end
                end
                1: begin
                    e = -4;
                    for (k = -4; k <= 7; k = k + 1)
                    if ((64'd224 << 4) >= (64'(long_side) << (k + 4))) e = k;
                    exponent <= e;
                    state <= 2;
                end
                2: begin
                    numerator <= 64'd224 << (52 - exponent);
                    denominator <= long_side;
                    state <= 3;
                end
                3: begin
                    div_start <= 1;
                    state <= 4;
                end
                4:
                if (div_done) begin
                    scale_mantissa<=quotient[52:0]+((2*remainder>denominator)||((2*remainder==denominator)&&quotient[0]));
                    state <= 5;
                end
                5: begin
                    product_low <= scale_mantissa[17:0] * short_side;
                    product_mid <= scale_mantissa[35:18] * short_side;
                    product_high <= scale_mantissa[52:36] * short_side;
                    state <= 6;
                end
                6: begin
                    combined_low <= 47'(product_low) + (47'(product_mid) << 18);
                    state <= 7;
                end
                7: begin
                    product <= 64'(combined_low) + (64'(product_high) << 36);
                    state   <= 8;
                end
                8: begin
                    b = 0;
                    for (k = 0; k < 64; k = k + 1) if (product[k]) b = k;
                    drop  <= b > 52 ? b - 52 : 0;
                    state <= 9;
                end
                9: begin
                    head <= product >> drop;
                    integer_shift <= 6'(52 - exponent) - drop;
                    tail = product[10:0] & (11'h7ff >> (11 - drop));
                    half = 11'b1 << (drop - 1);
                    increment <= drop != 0 && ((tail > half) || ((tail == half) && product[drop]));
                    state <= 10;
                end
                10: begin
                    rounded_head <= {1'b0, head} + increment;
                    state <= 11;
                end
                11: begin
                    raw_dimension<=integer_shift>54?0:(rounded_head>>integer_shift)+rounded_head[integer_shift-1];
                    state <= 12;
                end
                12: begin
                    dim = raw_dimension < 1 ? 1 : raw_dimension > 224 ? 224 : raw_dimension;
                    resize_width <= width_major ? 224 : dim;
                    resize_height <= width_major ? dim : 224;
                    pad_left <= width_major ? 0 : (224 - dim) / 2;
                    pad_top <= width_major ? (224 - dim) / 2 : 0;
                    done <= 1;
                    busy <= 0;
                    state <= 0;
                end
            endcase
        end
endmodule
