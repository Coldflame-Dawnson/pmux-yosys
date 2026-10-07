`define P0_P9_OP 8'b10101010
`define P0_P3_OP 8'b11111111
`define P4_P7_OP 8'b11111110

module test3 (clk, cs, sr_in, gpioout, sr_out);
input clk, cs;
input sr_in;
output sr_out;
output [7:0] gpioout;

reg [7:0] gpioout;
reg sr_out;
reg [7:0] sr;

always @(posedge clk) begin
    if (cs == 1'b0) begin
        sr_out <= sr[7];
        sr[7:1] <= sr[6:0];
        sr[0] <= sr_in;
    end

    // The original inner block is observable only under sr[7] == 1.
    if (cs == 1'b1 && sr[7] == 1'b1) begin
        case (sr[6:0])
            7'h2a: gpioout[7:0] <= 8'h55;
            7'h7f: gpioout[3:0] <= 4'hf;
            7'h7e: gpioout[7:4] <= 4'hf;
            default: gpioout[0] <= sr[0];
        endcase
    end
end
endmodule
