module lut4 #(parameter [15:0] INIT = 16'h0000) (
    input  wire [3:0] a,
    output wire       y
);
assign y = INIT[a];
endmodule

module test4 (
    input wire clk,
    input wire reset_n,
    input wire ena,
    input wire [4:0] datain,
    output reg [5:0] dataout,
    output reg only_5b,
    output reg datain_5bis7,
    output reg data_dp,
    output reg data_dn
);
wire [9:0] lo;
wire [9:0] hi;
wire [9:0] rom_value = datain[4] ? hi : lo;

lut4 #(.INIT(16'h6800)) lo0 (.a(datain[3:0]), .y(lo[0]));
lut4 #(.INIT(16'h0000)) hi0 (.a(datain[3:0]), .y(hi[0]));
lut4 #(.INIT(16'h0000)) lo1 (.a(datain[3:0]), .y(lo[1]));
lut4 #(.INIT(16'h0016)) hi1 (.a(datain[3:0]), .y(hi[1]));
lut4 #(.INIT(16'h0080)) lo2 (.a(datain[3:0]), .y(lo[2]));
lut4 #(.INIT(16'h0000)) hi2 (.a(datain[3:0]), .y(hi[2]));
lut4 #(.INIT(16'h7e68)) lo3 (.a(datain[3:0]), .y(lo[3]));
lut4 #(.INIT(16'h167e)) hi3 (.a(datain[3:0]), .y(hi[3]));
lut4 #(.INIT(16'h977f)) lo4 (.a(datain[3:0]), .y(lo[4]));
lut4 #(.INIT(16'h8117)) hi4 (.a(datain[3:0]), .y(hi[4]));
lut4 #(.INIT(16'h8001)) lo5 (.a(datain[3:0]), .y(lo[5]));
lut4 #(.INIT(16'hffff)) hi5 (.a(datain[3:0]), .y(hi[5]));
lut4 #(.INIT(16'hfe17)) lo6 (.a(datain[3:0]), .y(lo[6]));
lut4 #(.INIT(16'h7e00)) hi6 (.a(datain[3:0]), .y(hi[6]));
lut4 #(.INIT(16'h71e6)) lo7 (.a(datain[3:0]), .y(lo[7]));
lut4 #(.INIT(16'hf0f1)) hi7 (.a(datain[3:0]), .y(hi[7]));
lut4 #(.INIT(16'hcdda)) lo8 (.a(datain[3:0]), .y(lo[8]));
lut4 #(.INIT(16'h4dcd)) hi8 (.a(datain[3:0]), .y(hi[8]));
lut4 #(.INIT(16'h2bbd)) lo9 (.a(datain[3:0]), .y(lo[9]));
lut4 #(.INIT(16'habaa)) hi9 (.a(datain[3:0]), .y(hi[9]));

always @(posedge clk or negedge reset_n) begin
    if (!reset_n) begin
        dataout <= 6'b000000;
        only_5b <= 1'b1;
        data_dp <= 1'b0;
        data_dn <= 1'b0;
    end else if (!ena) begin
        {dataout, only_5b, datain_5bis7, data_dp, data_dn} <= 10'b0;
    end else begin
        {dataout, only_5b, datain_5bis7, data_dp, data_dn} <= rom_value;
    end
end
endmodule
