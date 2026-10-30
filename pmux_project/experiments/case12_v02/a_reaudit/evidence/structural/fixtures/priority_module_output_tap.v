module top(input [3:0] s, input [3:0] d, output [0:0] y, output [0:0] tap);
  wire [0:0] m0 = s[0] ? d[0 +: 1] : 1'd0;
  wire [0:0] m1 = s[1] ? d[1 +: 1] : m0;
  wire [0:0] m2 = s[2] ? d[2 +: 1] : m1;
  wire [0:0] m3 = s[3] ? d[3 +: 1] : m2;
  assign y = m3;
  assign tap = m1;
endmodule
