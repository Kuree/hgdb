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
