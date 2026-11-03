`timescale 1ns / 1ps
// Read-only addition to the actual User_Ddr port. Original adder writes remain
// untouched. Single outstanding read owner, retained until the documented last bit.
module xj_customop_read_path (
    input wire gp_clk,
    hp_clk,
    resetn,
    gp_resetn,
    input wire req_valid,
    output wire req_ready,
    input wire [31:0] req_addr,
    output wire rsp_valid,
    input wire rsp_ready,
    output wire [511:0] rsp_data,
    output wire [31:0] rsp_info,
    output wire rsp_error,
    input wire [31:0] legacy_araddr,
    input wire legacy_arvalid,
    output wire legacy_arready,
    output wire [511:0] legacy_rdata,
    output wire legacy_rvalid,
    input wire legacy_rready,
    output wire [31:0] araddr,
    output wire arvalid,
    input wire arready,
    input wire [511:0] rdata,
    input wire [31:0] rinfo,
    input wire rvalid,
    output wire rready
);
    wire cv, cr, rv, rr;
    wire [31:0] cp;
    reg [544:0] response;
    xj_command_cdc #(
        .REQUEST_BITS (32),
        .RESPONSE_BITS(545)
    ) request_cdc (
        .s_clk(gp_clk),
        .s_resetn(gp_resetn),
        .s_valid(req_valid),
        .s_ready(req_ready),
        .s_payload(req_addr),
        .s_rsp_valid(rsp_valid),
        .s_rsp_ready(rsp_ready),
        .s_response({rsp_error, rsp_info, rsp_data}),
        .m_clk(hp_clk),
        .m_resetn(resetn),
        .m_valid(cv),
        .m_ready(cr),
        .m_payload(cp),
        .m_rsp_valid(rv),
        .m_rsp_ready(rr),
        .m_response(response)
    );
    reg [1:0] state;
    reg [31:0] address;
    reg malformed;
    wire n_arvalid = state == 1;
    wire n_arready, n_rvalid;
    wire n_rready = state == 2;
    assign cr = state == 0;
    assign rv = state == 3;
    reg active, owner, sent, turn;
    wire pick = (legacy_arvalid && n_arvalid) ? turn : n_arvalid;
    assign araddr = owner ? address : legacy_araddr;
    assign arvalid = active && !sent && (owner ? n_arvalid : legacy_arvalid);
    assign legacy_arready = active && !owner && !sent && arready;
    assign n_arready = active && owner && !sent && arready;
    assign legacy_rdata = rdata;
    assign legacy_rvalid = active && !owner && sent && rvalid;
    assign n_rvalid = active && owner && sent && rvalid;
    assign rready = active && sent && (owner ? n_rready : legacy_rready);
    always @(posedge hp_clk) begin
        if (!resetn) begin
            state <= 0;
            address <= 0;
            malformed <= 0;
            response <= 0;
            active <= 0;
            owner <= 0;
            sent <= 0;
            turn <= 0;
        end else begin
            if (!active) begin
                if (legacy_arvalid || n_arvalid) begin
                    active <= 1;
                    owner  <= pick;
                    sent   <= 0;
                end
            end else begin
                if (arvalid && arready) sent <= 1;
                if (rvalid && rready && rinfo[0]) begin
                    active <= 0;
                    turn   <= !owner;
                end
            end
            case (state)
                0:
                if (cv && cr) begin
                    address   <= cp;
                    malformed <= 0;
                    if (cp[5:0] != 0) begin
                        response <= {1'b1, 544'b0};
                        state <= 3;
                    end else state <= 1;
                end
                1: if (n_arready) state <= 2;
                2:
                if (n_rvalid && n_rready) begin
                    if (rinfo[0]) begin
                        response <= {malformed, rinfo, rdata};
                        state <= 3;
                    end else
                        malformed<=1; // Drain a malformed multi-packet return before releasing ownership.
                end
                3: if (rr) state <= 0;
            endcase
        end
    end
endmodule
