module cycloneiv_lcell_comb #(
    parameter [15:0] lut_mask = 16'hffff,
    parameter dont_touch = "off",
    parameter lpm_type = "cycloneiv_lcell_comb",
    parameter sum_lutc_input = "datac"
) (
    output wire combout,
    output wire cout,
    input wire dataa,
    input wire datab,
    input wire datac,
    input wire datad,
    input wire cin
);
    assign combout = lut_mask[{datad, datac, datab, dataa}];
    assign cout = 1'b0;
endmodule
