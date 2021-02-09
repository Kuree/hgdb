module child (input logic clk);
logic [3:0][1:0][15:0] a;
logic [15:0] b[3:0][1:0];
endmodule

module top;

logic clk;

child dut(.*);

initial clk = 0;
always clk = #5 ~clk;

initial begin
    $dumpfile("waveform4.vcd");
    $dumpvars(0, top);
    for (int j = 0; j < 2; j++) begin
        for (int i = 0; i < 4; i++) begin
            for (int k = 0; k < 2; k++) begin
                dut.a[i][k] = i + 1 + j * 10 + k;
                dut.b[i][k] = i + 2 + j * 10 + k;
            end
        end
        #10;
    end
    $finish;
end

endmodule
