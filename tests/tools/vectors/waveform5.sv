module child1 (
    input logic io_a_b,
    input logic io_a_c
);

endmodule

module child2 (
    input logic io_a_b,
    input logic io_a_c
);

// unpacked array
logic [15:0] a[3:0];

child1 inst1 (.*);
child1 inst2 (.*);

endmodule


module top;

logic io_a_b, io_a_c;

child2 dut (.*);

initial begin
    $dumpfile("waveform5.vcd");
    $dumpvars(0, top);

    for (int i = 0; i < 4; i++) begin
        io_a_b = i & 1;
        io_a_c = i ^ 1;
        for (int j = 0; j < $size(dut.a); j++) begin
            dut.a[j] = i + 1;
        end
        #10;
    end
end

endmodule
