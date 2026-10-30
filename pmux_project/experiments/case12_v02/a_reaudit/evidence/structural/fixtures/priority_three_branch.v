module top(input [2:0] s, input [2:0] d, output [0:0] y);
  wire [0:0] m0 = s[0] ? d[0 +: 1] : 1'd0;
  wire [0:0] m1 = s[1] ? d[1 +: 1] : m0;
  wire [0:0] m2 = s[2] ? d[2 +: 1] : m1;
  assign y = m2;
endmodule
