module mod (
    input  logic      clk,
    input  logic      rst,
    input  logic[3:0] in,
    output logic[3:0] out
);

logic[3:0] data;

always_ff @(posedge clk) begin
    if (rst) begin
        data <= 0;
        out <= 0;
    end begin
        data <= in;
        out <= data;
    end
end
endmodule


module top;

logic clk, rst;
logic[3:0] in, out;

mod dut(.*);

initial clk = 0;
always clk = #5 ~clk;

initial begin
    rst = 1;
    @(posedge clk);
    rst = 0;
    @(posedge clk);

    for (int i = 0; i < 16; i++) begin
        in = i;
        @(posedge clk);
    end
    $finish;
end


endmodule
