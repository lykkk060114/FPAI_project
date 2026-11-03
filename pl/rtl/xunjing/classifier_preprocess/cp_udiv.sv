// Unsigned restoring divider; one transaction at a time, reset cancels it.
module cp_udiv (
    input wire clk,
    resetn,
    start,
    input wire [63:0] numerator,
    input wire [15:0] denominator,
    output reg done,
    output reg [63:0] quotient,
    output reg [16:0] remainder
);
    reg busy;
    reg [6:0] count;
    reg [63:0] bits;
    reg [15:0] den;
    wire [17:0] trial = {remainder, bits[63]};
    always @(posedge clk)
        if (!resetn) begin
            busy <= 0;
            done <= 0;
            quotient <= 0;
            remainder <= 0;
            count <= 0;
            bits <= 0;
            den <= 1;
        end else begin
            done <= 0;
            if (start && !busy) begin
                busy <= 1;
                count <= 64;
                bits <= numerator;
                den <= denominator;
                quotient <= 0;
                remainder <= 0;
            end else if (busy) begin
                bits <= bits << 1;
                quotient <= {quotient[62:0], trial >= {2'b0, den}};
                remainder <= trial >= den ? trial - den : trial;
                count <= count - 1;
                if (count == 1) begin
                    busy <= 0;
                    done <= 1;
                end
            end
        end
endmodule
