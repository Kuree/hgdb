module top;

logic clk, rst;
logic[3:0] in, out;

mod dut(.*);

initial clk = 0;
always clk = #5 ~clk;

initial begin
    rst = 1;
    in = 0;
    @(posedge clk);
    rst = 0;
    @(posedge clk);

    for (int i = 0; i < 16; i++) begin
        in = i + 1;
        @(posedge clk);
    end
    $finish;
end


endmodule
