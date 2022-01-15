module top;

logic clk, rst;
logic [15:0] in;
logic [1:0] addr;

mod dut (.*);

initial clk = 0;
always clk = #10 ~clk;

initial begin
    int sum;
    sum = 0;
    // reset
    rst = 0;
    #1 rst = 1;
    #1 rst = 0;
    for (int i = 1; i <= 4; i++) begin
        in = i + 1;
        addr = i;
        @(posedge clk);
        @(negedge clk);
    end
    $finish(0);
end

endmodule
