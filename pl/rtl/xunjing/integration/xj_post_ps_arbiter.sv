// Shares the PS word port between CLASS and POST transactions.
// Ownership remains assigned until the selected response is accepted.
module xj_post_ps_arbiter (
    input wire clk,
    input wire resetn,

    input  wire        class_valid,
    output wire        class_ready,
    input  wire        class_write,
    input  wire [31:0] class_addr,
    input  wire [31:0] class_data,
    output wire        class_rsp_valid,
    input  wire        class_rsp_ready,

    input  wire        post_valid,
    output wire        post_ready,
    input  wire [31:0] post_addr,
    input  wire [31:0] post_data,
    output wire        post_rsp_valid,
    input  wire        post_rsp_ready,

    output wire        mem_valid,
    input  wire        mem_ready,
    output wire        mem_write,
    output wire [31:0] mem_addr,
    output wire [31:0] mem_data,
    input  wire        mem_rsp_valid,
    output wire        mem_rsp_ready
);

    localparam [1:0] IDLE = 2'd0;
    localparam [1:0] REQUEST = 2'd1;
    localparam [1:0] RESPONSE = 2'd2;

    reg [1:0] state;
    reg owner;
    reg turn;

    wire selected_valid = owner ? post_valid : class_valid;

    assign mem_valid = (state == REQUEST) && selected_valid;
    assign mem_addr = owner ? post_addr : class_addr;
    assign mem_data = owner ? post_data : class_data;
    assign mem_write = owner ? 1'b1 : class_write;

    assign class_ready = (state == REQUEST) && !owner && mem_ready;
    assign post_ready = (state == REQUEST) && owner && mem_ready;

    assign class_rsp_valid = (state == RESPONSE) && !owner && mem_rsp_valid;
    assign post_rsp_valid = (state == RESPONSE) && owner && mem_rsp_valid;
    assign mem_rsp_ready = (state == RESPONSE) && (owner ? post_rsp_ready : class_rsp_ready);

    always @(posedge clk) begin
        if (!resetn) begin
            state <= IDLE;
            owner <= 1'b0;
            turn  <= 1'b0;
        end else begin
            case (state)
                IDLE: begin
                    if (class_valid || post_valid) begin
                        owner <= class_valid && post_valid ? turn : post_valid;
                        state <= REQUEST;
                    end
                end

                REQUEST: begin
                    if (!selected_valid) state <= IDLE;
                    else if (mem_ready) state <= RESPONSE;
                end

                RESPONSE: begin
                    if (mem_rsp_valid && mem_rsp_ready) begin
                        turn  <= !owner;
                        state <= IDLE;
                    end
                end

                default: state <= IDLE;
            endcase
        end
    end

endmodule
