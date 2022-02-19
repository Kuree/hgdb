interface interface_a;
    logic [15:0] a;
    logic [15:0][3:0] b;
    logic [15:0] c[1:0];

endinterface

typedef struct packed {
    logic [15:0] a;
    logic [15:0][3:0] b;
} struct_b;

module top(
    input logic clk
);

interface_a a();
struct_b b;

always_ff @(posedge clk) begin
    b.a <= 1;
end

endmodule
