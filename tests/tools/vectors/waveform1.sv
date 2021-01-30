module child(input logic clk,
             input logic a,
             output logic [1:0] b);

always_ff @(posedge clk) begin
    b <= {a, ~a};
end

endmodule

module top;

localparam num_cycles = 10;
logic clk, a;
logic [1:0] b;
logic [1:0] result[num_cycles-1:0];

child inst (.*);

initial clk = 0;
always clk = #10 ~clk;

initial begin
    $dumpfile("waveform1.vcd");
    $dumpvars(0, top);

    for (int i = 0; i < num_cycles; i++) begin
        a = i & 1;
        @(posedge clk);
        @(negedge clk);
        result[i] = b;
    end

    $finish;
end


endmodule
