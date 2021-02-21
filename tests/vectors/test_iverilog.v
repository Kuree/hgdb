module top;

reg a, clk, rst, b;
reg[31:0] i;

always @(posedge clk) begin
    if (rst)
        a <= 0;
    else
        a <= b;
end

initial begin
    clk = 0;
    rst = 1;
    #5;
    clk = 1;
    #5;
    clk = 0;
    rst = 0;
    #0;

    for (i = 0; i < 10; i++) begin
        clk = 1;
        b = i % 2;
        #5;
        clk = 0;
        #5;
    end

end

endmodule
