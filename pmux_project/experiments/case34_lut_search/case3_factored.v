module test3 (clk, cs, sr_in, gpioout, sr_out);
input clk, cs;
input sr_in;
output sr_out;
output [7:0] gpioout;

reg [7:0] gpioout;
reg sr_out;
reg [7:0] sr;

wire sel_a = sr[6:0] == 7'b0101010;
wire sel_b = sr[6:0] == 7'b1111111;
wire sel_c = sr[6:0] == 7'b1111110;

always @(posedge clk) begin
    if (!cs) begin
        sr_out <= sr[7];
        sr <= {sr[6:0], sr_in};
    end

    if (cs && sr[7]) begin
        if (sel_a || sel_b)
            gpioout[3:0] <= sel_a ? 4'b0101 : 4'b1111;
        else if (!sel_c)
            gpioout[0] <= sr[0];

        if (sel_a || sel_c)
            gpioout[7:4] <= sel_a ? 4'b0101 : 4'b1111;
    end
end
endmodule
