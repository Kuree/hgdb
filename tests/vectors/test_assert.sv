module child #(parameter id = 0) (
    input logic clk
);
initial begin
    assert (id == 1) else $hgdb_assert_fail(child, "test.py", 1);
end
endmodule

module top;

logic clk;

child #(.id(1)) inst1(clk);
child #(.id(2)) inst2(clk);

initial clk = 0;
always clk = #5 ~clk;

initial begin
    #20;
    $finish;
end

endmodule
