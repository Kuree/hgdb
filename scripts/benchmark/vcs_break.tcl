# change clk signal name accordingly based on your design
set clk "top.clk"
stop -posedge $clk
while {1} {
    run
    get $clk -radix d
}
