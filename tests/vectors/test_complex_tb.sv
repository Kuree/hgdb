module tb;

logic clk;

top dut(.*);

initial begin
    for (int i = 0; i < 4; i++) begin
        clk = 0;
        #5;
        clk = 1;
        #5;
    end
end

endmodule
