`timescale 1ns / 1ps
// Dual-clock block RAM FIFO, with registered read output and Gray pointers.
// Both sides reset from the same HP path reset; a camera soft reset must not
// reset only one pointer. Depth is a power of two.
module xj_pixel_fifo #(
    parameter A = 12
) (
    input wire wclk,
    wrstn,
    wvalid,
    input wire [23:0] wdata,
    output wire wready,
    input wire rclk,
    rrstn,
    output reg rvalid,
    input wire rready,
    output reg [23:0] rdata
);
    (* ram_style="block" *) reg [23:0] ram[0:(1<<A)-1];
    reg [A:0] wb, wg, rb, rg;
    reg full;
    (* ASYNC_REG="TRUE" *) reg [A:0] rs1, rs2, ws1, ws2;
    wire push = wvalid && wready;
    wire [A:0] wn = wb + push, wgn = (wn >> 1) ^ wn;
    wire empty = rg == ws2;
    assign wready = !full;
    // RAM ports must have no asynchronous reset sensitivity (Vivado BRAM template).
    always @(posedge wclk) if (wrstn && push) ram[wb[A-1:0]] <= wdata;
    always @(posedge wclk or negedge wrstn) begin
        if (!wrstn) begin
            wb   <= 0;
            wg   <= 0;
            rs1  <= 0;
            rs2  <= 0;
            full <= 0;
        end else begin
            rs1  <= rg;
            rs2  <= rs1;
            wb   <= wn;
            wg   <= wgn;
            full <= wgn == {~rs2[A:A-1], rs2[A-2:0]};
        end
    end
    wire fetch = (!rvalid || rready) && !empty;
    wire [A:0] rn = rb + fetch;
    always @(posedge rclk) if (rrstn && fetch) rdata <= ram[rb[A-1:0]];
    always @(posedge rclk or negedge rrstn) begin
        if (!rrstn) begin
            rb <= 0;
            rg <= 0;
            ws1 <= 0;
            ws2 <= 0;
            rvalid <= 0;
        end else begin
            ws1 <= wg;
            ws2 <= ws1;
            if (!rvalid || rready) begin
                rvalid <= !empty;
                if (!empty) begin
                    rb <= rn;
                    rg <= (rn >> 1) ^ rn;
                end
            end
        end
    end
endmodule
