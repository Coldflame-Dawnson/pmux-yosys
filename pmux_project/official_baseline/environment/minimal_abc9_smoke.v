module minimal_abc9_smoke(
    input  wire       clk,
    input  wire [3:0] a,
    input  wire [3:0] b,
    input  wire       sel,
    output reg  [3:0] y
);
    always @(posedge clk)
        y <= sel ? (a ^ b) : (a + b);
endmodule
