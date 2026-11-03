`timescale 1ns / 1ps
// Single-outstanding HP transaction adapter. A write response is returned only
// after B, never merely after AW/W. The caller owns all addressed allocations.
module xj_axi64_word_port (
    input wire clk,
    resetn,
    input wire req_valid,
    output wire req_ready,
    input wire req_write,
    input wire [31:0] req_addr,
    req_data,
    output reg rsp_valid,
    input wire rsp_ready,
    output reg [15:0] rsp_data,
    output reg rsp_error,
    output reg [31:0] awaddr,
    output reg awvalid,
    input wire awready,
    output reg [63:0] wdata,
    output reg [7:0] wstrb,
    output reg wvalid,
    input wire wready,
    input wire bvalid,
    output wire bready,
    input wire [1:0] bresp,
    output reg [31:0] araddr,
    output reg arvalid,
    input wire arready,
    input wire [63:0] rdata,
    input wire [1:0] rresp,
    input wire rlast,
    rvalid,
    output wire rready
);
    localparam IDLE = 0, WRITE = 1, READ_ADDR = 2, READ_DATA = 3, RESPONSE = 4;
    reg [2:0] state;
    reg [1:0] lane;
    reg bad_read;
    assign req_ready = state == IDLE;
    assign bready = state == WRITE && !awvalid && !wvalid;
    assign rready = state == READ_DATA;
    always @(posedge clk) begin
        if (!resetn) begin
            state <= IDLE;
            rsp_valid <= 0;
            rsp_data <= 0;
            rsp_error <= 0;
            awaddr <= 0;
            awvalid <= 0;
            wdata <= 0;
            wstrb <= 0;
            wvalid <= 0;
            araddr <= 0;
            arvalid <= 0;
            lane <= 0;
            bad_read <= 0;
        end else
            case (state)
                IDLE:
                if (req_valid) begin
                    rsp_error <= 0;
                    rsp_data  <= 0;
                    if (req_addr[0] || (req_write && req_addr[1])) begin
                        rsp_error <= 1;
                        rsp_valid <= 1;
                        state <= RESPONSE;
                    end else if (req_write) begin
                        awaddr  <= {req_addr[31:3], 3'b0};
                        awvalid <= 1;
                        wdata   <= {req_data, req_data};
                        wstrb   <= req_addr[2] ? 8'hf0 : 8'h0f;
                        wvalid  <= 1;
                        state   <= WRITE;
                    end else begin
                        araddr <= {req_addr[31:3], 3'b0};
                        arvalid <= 1;
                        lane <= req_addr[2:1];
                        bad_read <= 0;
                        state <= READ_ADDR;
                    end
                end
                WRITE: begin
                    if (awvalid && awready) awvalid <= 0;
                    if (wvalid && wready) wvalid <= 0;
                    if (bvalid && bready) begin
                        rsp_error <= bresp != 0;
                        rsp_valid <= 1;
                        state <= RESPONSE;
                    end
                end
                READ_ADDR:
                if (arready) begin
                    arvalid <= 0;
                    state   <= READ_DATA;
                end
                READ_DATA:
                if (rvalid) begin
                    // A malformed extra beat must be drained before accepting a new request.
                    bad_read <= bad_read || rresp != 0 || !rlast;
                    if (rlast) begin
                        rsp_data <= rdata >> (lane * 16);
                        rsp_error <= bad_read || rresp != 0;
                        rsp_valid <= 1;
                        state <= RESPONSE;
                    end
                end
                RESPONSE:
                if (rsp_ready) begin
                    rsp_valid <= 0;
                    state <= IDLE;
                end
                default: state <= IDLE;
            endcase
    end
endmodule
