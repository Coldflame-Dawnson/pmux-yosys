// Formal model for the dffeas subset emitted by Yosys 0.69
// techlibs/intel/common/ff_map.v.  The baseline checker separately rejects
// instances whose PRN/ASDATA/ALOAD/SCLR/SLOAD pins do not have the constants
// used by that mapping.
module dffeas (
    output reg q,
    input wire d,
    input wire clk,
    input wire clrn,
    input wire prn,
    input wire ena,
    input wire asdata,
    input wire aload,
    input wire sclr,
    input wire sload
);
    parameter power_up = "dontcare";
    parameter is_wysiwyg = "false";

    always @(posedge clk or negedge clrn) begin
        if (!clrn)
            q <= 1'b0;
        else if (ena)
            q <= d;
    end
endmodule
