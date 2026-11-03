// OpenCV 4.6 INTER_LINEAR coefficient generation. Rational coordinate is first
// rounded to binary32, then coefficients to Q11, ties-to-even at both steps.
module cp_axis (
    input wire clk,
    resetn,
    start,
    input wire [11:0] src_size,
    input wire [7:0] dst_size,
    index,
    input wire horizontal,
    output reg done,
    output reg [11:0] lo,
    hi,
    a0,
    a1
);
    reg [3:0] state;
    reg [11:0] size;
    reg hor, negative;
    reg signed [5:0] exponent;
    reg [63:0] numerator;
    reg [15:0] denominator;
    reg div_start;
    wire div_done;
    wire [63:0] quotient;
    wire [16:0] remainder;
    reg [63:0] mantissa, coordinate_mag;
    reg signed [63:0] coordinate;
    reg signed [21:0] n_reg;
    reg [20:0] magnitude;
    reg signed [31:0] position_reg;
    reg [11:0] coef_reg;
    integer n, absn, e, k, position, coef;
    reg [63:0] fraction;
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
            done <= 0;
            div_start <= 0;
            lo <= 0;
            hi <= 0;
            a0 <= 0;
            a1 <= 0;
        end else begin
            done <= 0;
            div_start <= 0;
            case (state)
                0:
                if (start) begin
                    n_reg <= $signed(22'((2 * index + 1) * src_size)) - $signed(22'(dst_size));
                    size <= src_size;
                    hor <= horizontal;
                    denominator <= 2 * dst_size;
                    state <= 1;
                end
                1: begin
                    magnitude <= n_reg < 0 ? -n_reg : n_reg;
                    negative <= n_reg < 0;
                    state <= 2;
                end
                2: begin
                    e = -10;
                    for (k = -10; k <= 11; k = k + 1)
                    if ((64'(magnitude) << 10) >= (64'(denominator) << (k + 10))) e = k;
                    exponent <= e;
                    state <= 3;
                end
                3: begin
                    numerator <= 64'(magnitude) << (23 - exponent);
                    state <= 4;
                end
                4: begin
                    div_start <= 1;
                    state <= 5;
                end
                5:
                if (div_done) begin
                    mantissa<=quotient+((2*remainder>denominator)||((2*remainder==denominator)&&quotient[0]));
                    state <= 6;
                end
                6: begin
                    coordinate_mag <= mantissa << (exponent + 9);
                    state <= 7;
                end
                7: begin
                    coordinate <= negative ? -$signed(coordinate_mag) : $signed(coordinate_mag);
                    state <= 8;
                end
                8: begin
                    position_reg <= $signed(coordinate[63:32]);
                    coef_reg<={1'b0,coordinate[31:21]}+((coordinate[20:0]>21'h100000)||((coordinate[20:0]==21'h100000)&&coordinate[21]));
                    state <= 9;
                end
                9: begin
                    lo <= position_reg < 0 ? 0 : position_reg >= size ? size - 1 : position_reg;
                    hi<=position_reg+1<0?0:position_reg+1>=size?size-1:position_reg+1;
                    a0<=hor&&(position_reg<0||position_reg>=size-1)?2048:2048-coef_reg;
                    a1 <= hor && (position_reg < 0 || position_reg >= size - 1) ? 0 : coef_reg;
                    done <= 1;
                    state <= 0;
                end
            endcase
        end
endmodule
