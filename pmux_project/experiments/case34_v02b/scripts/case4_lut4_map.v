(* techmap_celltype = "case4_lut4" *)
module case4_lut4_map #(parameter [15:0] LUT = 16'h0000)
(
    input [3:0] A,
    output Y
);
    \$lut #(.WIDTH(4), .LUT(LUT)) _TECHMAP_REPLACE_ (.A(A), .Y(Y));
endmodule
