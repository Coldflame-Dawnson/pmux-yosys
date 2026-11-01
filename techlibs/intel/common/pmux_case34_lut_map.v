(* techmap_celltype = "pmux_case34_lut4" *)
module pmux_case34_lut4_map #(
    parameter [15:0] LUT = 16'h0000
) (
    input [3:0] A,
    output Y
);
    \$lut #(.WIDTH(4), .LUT(LUT)) _TECHMAP_REPLACE_ (.A(A), .Y(Y));
endmodule
