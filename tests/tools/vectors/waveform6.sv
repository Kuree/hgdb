module child2(input logic clk);
endmodule

module child(input logic clk,
             input logic a,
             output logic [1:0] b);

child2 inst(.*);

always_ff @(posedge clk) begin
    b <= {a, ~a};
end

endmodule

typedef struct packed {
    logic a;
    logic b;
} test_struct;

module top;

localparam num_cycles = 10;
logic clk, a;
logic [1:0] b;
logic [1:0] result[num_cycles-1:0];

test_struct test_s;

child inst (.*);

initial clk = 0;
always clk = #10 ~clk;

initial begin
    $fsdbDumpfile("waveform6.fsdb");
    $fsdbDumpvars(0);
    $fsdbDumpvars("+all");

    for (int i = 0; i < num_cycles; i++) begin
        a = i & 1;
        @(posedge clk);
        @(negedge clk);
        result[i] = b;
        test_s.a = i & 1;
        test_s.b = ~(i & 1);
    end

    $finish;
end


endmodule
