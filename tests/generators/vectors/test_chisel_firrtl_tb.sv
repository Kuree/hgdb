module top;

logic clock, reset;

SimpleVendingMachineTester tester(.*);

initial clock = 0;
always clock = #5 ~clock;

initial begin
    $dumpfile("waveform.vcd");
    $dumpvars(0, top);
    reset = 1;
    repeat (2) @(posedge clock);
    reset = 0;
    for (int i = 0; i < 10; i++) begin
        @(posedge clock);
    end
    $finish;
end

endmodule
