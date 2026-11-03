`timescale 1ns / 1ps
// Assert with the complete HP path; release synchronously in the consumer clock.
module xj_reset_release (
    input  wire clk,
    async_reset,
    output wire resetn
);
    (* ASYNC_REG="TRUE" *) reg [1:0] release_pipe;
    always @(posedge clk or posedge async_reset) begin
        if (async_reset) release_pipe <= 0;
        else release_pipe <= {release_pipe[0], 1'b1};
    end
    assign resetn = release_pipe[1];
endmodule
