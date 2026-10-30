module top(input [3:0] s, input [127:0] d, output [31:0] y);
  wire [31:0] m0 = s[0] ? d[0 +: 32] : 32'd0;
  wire [31:0] m1 = s[1] ? d[32 +: 32] : m0;
  wire [31:0] m2 = s[2] ? d[64 +: 32] : m1;
  wire [31:0] m3 = s[3] ? d[96 +: 32] : m2;
  assign y = m3;
endmodule
