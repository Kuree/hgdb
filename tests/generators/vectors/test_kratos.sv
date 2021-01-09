module top;

logic clk, rst;
logic [15:0] in, out;

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
        in = i;
        @(posedge clk);
        sum += i;
        @(negedge clk);
        assert (out == sum);
    end
    $finish(0);
end

endmodule
