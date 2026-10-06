module test3 (clk, cs, sr_in, gpioout, sr_out);
input clk, cs;
input sr_in;
output sr_out;
output [7:0] gpioout;

reg [7:0] gpioout;
reg sr_out;
reg [7:0] sr;

wire rw = sr[7];

always @(posedge clk) begin
    if (cs == 1'b0) begin
        sr_out <= sr[7];
        sr[7:1] <= sr[6:0];
        sr[0] <= sr_in;
    end

    if (cs == 1'b1 && rw == 1'b1) begin
        case (sr[6:0])
            7'b0101010: gpioout[7:0] <= 8'b01010101;
            7'b1111111: gpioout[3:0] <= 4'b1111;
            7'b1111110: gpioout[7:4] <= 4'b1111;
            default:    gpioout[0] <= sr[0];
        endcase
    end
end
endmodule
