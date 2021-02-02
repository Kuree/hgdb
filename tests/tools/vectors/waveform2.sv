module child3 (input logic a3);
endmodule

module child2(input logic a2);
child3 inst3(.a3(a2));
endmodule

module child1(input logic a1);
child2 inst2(.a2(a1));
endmodule

module top;
logic a;

child1 inst1(.a1(a));

initial begin
    $dumpfile("waveform2.vcd");
    $dumpvars(0, top);
    
    for (int i = 0; i < 10; i++) begin
        #10 a = i & 1;
    end
end

endmodule
