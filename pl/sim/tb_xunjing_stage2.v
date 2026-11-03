`timescale 1ns / 1ps
module tb_xunjing_stage2;
    reg clk=0;
    always #5 clk=~clk;
    reg rst=1, vs=0, de=0;
    reg [23:0] rgb=0;
    wire vs0,de0,vs1,de1;
    wire [23:0] rgb0,rgb1;
    integer n=0;
    integer i,j;
    xunjing_stage2_preprocess #(.GEOMETRY_ENABLE(0),.COLOR_MODE(1)) bypass
        (.clk(clk),.rst(rst),.in_vsync(vs),.in_de(de),.in_rgb(rgb),
         .out_vsync(vs0),.out_de(de0),.out_rgb(rgb0));
    xunjing_stage2_preprocess #(.GEOMETRY_ENABLE(1),.COLOR_MODE(0)) crop
        (.clk(clk),.rst(rst),.in_vsync(vs),.in_de(de),.in_rgb(rgb),
         .out_vsync(vs1),.out_de(de1),.out_rgb(rgb1));
    initial begin
        repeat(3) @(negedge clk);
        rst=0;
        vs=1;
        @(negedge clk); vs=0;
        for(j=0;j<1080;j=j+1) begin
            for(i=0;i<1920;i=i+1) begin
                @(negedge clk);
                de=1;
                rgb=24'hABCDEF;
                @(posedge clk);
                #1;
                if (!de0 || rgb0 !== 24'hA8CCE8) begin
                    // RGB565 quantization: AB->A8, CD->CC, EF->E8
                    $fatal(1,"Bypass/color failed at (%0d,%0d): %h",i,j,rgb0);
                end
                if (de1) n=n+1;
            end
            @(negedge clk); de=0;
            repeat(2) @(negedge clk);
        end
        if (n != 640*352) $fatal(1,"Geometry count mismatch %0d", n);
        $display("PASS: RGB565 quantization and 640x352 output pixels");
        $finish;
    end
endmodule
