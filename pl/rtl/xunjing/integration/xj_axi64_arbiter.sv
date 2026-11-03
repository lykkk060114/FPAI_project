`timescale 1ns / 1ps
// Two masters -> existing PS HP2 AXI3 port, at most one outstanding transaction
// in each direction. No interleaved W bursts; WID is the granted AWID.
module xj_axi64_arbiter (
    input wire clk,
    resetn,
    input wire [1:0][31:0] s_awaddr,
    input wire [1:0][4:0] s_awid,
    input wire [1:0][3:0] s_awlen,
    input wire [1:0][2:0] s_awsize,
    input wire [1:0][1:0] s_awburst,
    s_awlock,
    input wire [1:0][3:0] s_awcache,
    s_awqos,
    input wire [1:0][2:0] s_awprot,
    input wire [1:0] s_awvalid,
    output reg [1:0] s_awready,
    input wire [1:0][63:0] s_wdata,
    input wire [1:0][7:0] s_wstrb,
    input wire [1:0] s_wlast,
    s_wvalid,
    output reg [1:0] s_wready,
    output wire [1:0][4:0] s_bid,
    output wire [1:0][1:0] s_bresp,
    output reg [1:0] s_bvalid,
    input wire [1:0] s_bready,
    input wire [1:0][31:0] s_araddr,
    input wire [1:0][4:0] s_arid,
    input wire [1:0][3:0] s_arlen,
    input wire [1:0][2:0] s_arsize,
    input wire [1:0][1:0] s_arburst,
    s_arlock,
    input wire [1:0][3:0] s_arcache,
    s_arqos,
    input wire [1:0][2:0] s_arprot,
    input wire [1:0] s_arvalid,
    output reg [1:0] s_arready,
    output wire [1:0][4:0] s_rid,
    output wire [1:0][63:0] s_rdata,
    output wire [1:0][1:0] s_rresp,
    output wire [1:0] s_rlast,
    output reg [1:0] s_rvalid,
    input wire [1:0] s_rready,
    output wire [31:0] m_awaddr,
    output wire [4:0] m_awid,
    m_wid,
    output wire [3:0] m_awlen,
    output wire [2:0] m_awsize,
    output wire [1:0] m_awburst,
    m_awlock,
    output wire [3:0] m_awcache,
    m_awqos,
    output wire [2:0] m_awprot,
    output wire m_awvalid,
    input wire m_awready,
    output wire [63:0] m_wdata,
    output wire [7:0] m_wstrb,
    output wire m_wlast,
    m_wvalid,
    input wire m_wready,
    input wire [4:0] m_bid,
    input wire [1:0] m_bresp,
    input wire m_bvalid,
    output wire m_bready,
    output wire [31:0] m_araddr,
    output wire [4:0] m_arid,
    output wire [3:0] m_arlen,
    output wire [2:0] m_arsize,
    output wire [1:0] m_arburst,
    m_arlock,
    output wire [3:0] m_arcache,
    m_arqos,
    output wire [2:0] m_arprot,
    output wire m_arvalid,
    input wire m_arready,
    input wire [4:0] m_rid,
    input wire [63:0] m_rdata,
    input wire [1:0] m_rresp,
    input wire m_rlast,
    m_rvalid,
    output wire m_rready
);
    reg write_active, wo, aw_done, w_done, write_turn;
    reg [4:0] wid;
    reg [1:0] read_state;
    reg ro, read_turn;
    wire write_pick = (s_awvalid == 2'b11) ? write_turn : s_awvalid[1];
    wire read_pick = (s_arvalid == 2'b11) ? read_turn : s_arvalid[1];
    assign m_awaddr = s_awaddr[wo];
    assign m_awid = s_awid[wo];
    assign m_wid = wid;
    assign m_awlen = s_awlen[wo];
    assign m_awsize = s_awsize[wo];
    assign m_awburst = s_awburst[wo];
    assign m_awlock = s_awlock[wo];
    assign m_awcache = s_awcache[wo];
    assign m_awqos = s_awqos[wo];
    assign m_awprot = s_awprot[wo];
    assign m_awvalid = write_active && !aw_done && s_awvalid[wo];
    assign m_wdata = s_wdata[wo];
    assign m_wstrb = s_wstrb[wo];
    assign m_wlast = s_wlast[wo];
    assign m_wvalid = write_active && !w_done && s_wvalid[wo];
    assign s_bid = {m_bid, m_bid};
    assign s_bresp = {m_bresp, m_bresp};
    assign m_bready = write_active && aw_done && w_done && s_bready[wo];
    assign m_araddr = s_araddr[ro];
    assign m_arid = s_arid[ro];
    assign m_arlen = s_arlen[ro];
    assign m_arsize = s_arsize[ro];
    assign m_arburst = s_arburst[ro];
    assign m_arlock = s_arlock[ro];
    assign m_arcache = s_arcache[ro];
    assign m_arqos = s_arqos[ro];
    assign m_arprot = s_arprot[ro];
    assign m_arvalid = read_state == 1 && s_arvalid[ro];
    assign s_rid = {m_rid, m_rid};
    assign s_rdata = {m_rdata, m_rdata};
    assign s_rresp = {m_rresp, m_rresp};
    assign s_rlast = {m_rlast, m_rlast};
    assign m_rready = read_state == 2 && s_rready[ro];
    always @* begin
        s_awready = 0;
        s_wready  = 0;
        s_bvalid  = 0;
        s_arready = 0;
        s_rvalid  = 0;
        if (write_active) begin
            s_awready[wo] = !aw_done && m_awready;
            s_wready[wo]  = !w_done && m_wready;
            s_bvalid[wo]  = aw_done && w_done && m_bvalid;
        end
        if (read_state == 1) s_arready[ro] = m_arready;
        if (read_state == 2) s_rvalid[ro] = m_rvalid;
    end
    always @(posedge clk) begin
        if (!resetn) begin
            write_active <= 0;
            wo <= 0;
            aw_done <= 0;
            w_done <= 0;
            wid <= 0;
            write_turn <= 0;
            read_state <= 0;
            ro <= 0;
            read_turn <= 0;
        end else begin
            if (!write_active) begin
                if (|s_awvalid) begin
                    wo <= write_pick;
                    wid <= s_awid[write_pick];
                    write_active <= 1;
                    aw_done <= 0;
                    w_done <= 0;
                end
            end else begin
                if (m_awvalid && m_awready) aw_done <= 1;
                if (m_wvalid && m_wready && m_wlast) w_done <= 1;
                if (m_bvalid && m_bready) begin
                    write_active <= 0;
                    write_turn   <= !wo;
                end
            end
            case (read_state)
                0:
                if (|s_arvalid) begin
                    ro <= read_pick;
                    read_state <= 1;
                end
                1: if (m_arvalid && m_arready) read_state <= 2;
                2:
                if (m_rvalid && m_rready && m_rlast) begin
                    read_state <= 0;
                    read_turn  <= !ro;
                end
                default: read_state <= 0;
            endcase
        end
    end
endmodule
