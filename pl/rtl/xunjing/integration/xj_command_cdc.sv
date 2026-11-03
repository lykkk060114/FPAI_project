`timescale 1ns / 1ps

// One held payload in each direction. Reset must cover the full AXI path;
// software abort drains the current transaction without resetting this mailbox.
module xj_command_cdc #(
    parameter REQUEST_BITS = 65,
    parameter RESPONSE_BITS = 17
) (
    input  wire                     s_clk,
    input  wire                     s_resetn,
    input  wire                     s_valid,
    output wire                     s_ready,
    input  wire [ REQUEST_BITS-1:0] s_payload,
    output wire                     s_rsp_valid,
    input  wire                     s_rsp_ready,
    output wire [RESPONSE_BITS-1:0] s_response,

    input  wire                     m_clk,
    input  wire                     m_resetn,
    output wire                     m_valid,
    input  wire                     m_ready,
    output wire [ REQUEST_BITS-1:0] m_payload,
    input  wire                     m_rsp_valid,
    output wire                     m_rsp_ready,
    input  wire [RESPONSE_BITS-1:0] m_response
);

    reg req_toggle;
    reg ack_toggle;
    reg [REQUEST_BITS-1:0] request_hold;
    reg [RESPONSE_BITS-1:0] response_hold;

    // Destination registers keep held multi-bit payloads out of downstream
    // combinational logic. The handshake controls when each payload is used.
    (* DONT_TOUCH = "TRUE" *) reg [REQUEST_BITS-1:0] request_capture;
    (* DONT_TOUCH = "TRUE" *) reg [RESPONSE_BITS-1:0] response_capture;
    (* ASYNC_REG = "TRUE" *) reg [1:0] req_sync;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_sync;

    reg accepted;
    reg source_busy;

    assign s_ready     = !source_busy;
    assign s_rsp_valid = source_busy && (ack_sync[1] == req_toggle);
    assign s_response  = response_capture;

    assign m_valid     = (req_sync[1] != ack_toggle) && !accepted;
    assign m_payload   = request_capture;
    assign m_rsp_ready = accepted;

    always @(posedge s_clk or negedge s_resetn) begin
        if (!s_resetn) begin
            req_toggle       <= 0;
            request_hold     <= 0;
            ack_sync         <= 0;
            source_busy      <= 0;
            response_capture <= 0;
        end else begin
            ack_sync <= {ack_sync[0], ack_toggle};

            // Sample continuously; the synchronized acknowledgement controls
            // use, while the source keeps this payload stable until completion.
            response_capture <= response_hold;

            if (s_valid && s_ready) begin
                request_hold <= s_payload;
                req_toggle   <= !req_toggle;
                source_busy  <= 1;
            end

            if (s_rsp_valid && s_rsp_ready) source_busy <= 0;
        end
    end

    always @(posedge m_clk or negedge m_resetn) begin
        if (!m_resetn) begin
            ack_toggle      <= 0;
            req_sync        <= 0;
            response_hold   <= 0;
            accepted        <= 0;
            request_capture <= 0;
        end else begin
            req_sync <= {req_sync[0], req_toggle};
            request_capture <= request_hold;

            if (m_valid && m_ready) accepted <= 1;

            if (m_rsp_valid && m_rsp_ready) begin
                response_hold <= m_response;
                ack_toggle    <= req_sync[1];
                accepted      <= 0;
            end
        end
    end

endmodule
