module top(input [3:0] s, input [47:0] d, output [11:0] y);
  wire [11:0] m0 = s[0] ? d[0 +: 12] : 12'd0;
  wire [11:0] m1 = s[1] ? d[12 +: 12] : m0;
  wire [11:0] m2 = s[2] ? d[24 +: 12] : m1;
  wire [11:0] m3 = s[3] ? d[36 +: 12] : m2;
  assign y = m3;
endmodule
