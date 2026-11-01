module case4_lut4 #(parameter [15:0] LUT = 16'h0000)
(
    input [3:0] A,
    output Y
);
    assign Y = LUT[A];
endmodule

module test4(
    input clk,
    input reset_n,
    input ena,
    input [4:0] datain,
    output reg [5:0] dataout,
    output reg only_5b,
    output reg datain_5bis7,
    output reg data_dp,
    output reg data_dn
);
    wire [9:0] lookup_word;
    wire cf_0 = 1'b0;
    wire cf_1;
    case4_lut4 #(.LUT(16'h0068)) cf_1_cell (.A({datain[4],datain[2],datain[1],datain[0]}), .Y(cf_1));
    wire cf_2;
    case4_lut4 #(.LUT(16'h1600)) cf_2_cell (.A({datain[4],datain[2],datain[1],datain[0]}), .Y(cf_2));
    wire cf_3;
    case4_lut4 #(.LUT(16'h0008)) cf_3_cell (.A({datain[4],datain[3],datain[2],datain[0]}), .Y(cf_3));
    wire cf_4;
    case4_lut4 #(.LUT(16'h7e68)) cf_4_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_4));
    wire cf_5;
    case4_lut4 #(.LUT(16'h167e)) cf_5_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_5));
    wire cf_6;
    case4_lut4 #(.LUT(16'h977f)) cf_6_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_6));
    wire cf_7;
    case4_lut4 #(.LUT(16'h8117)) cf_7_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_7));
    wire cf_8;
    case4_lut4 #(.LUT(16'h8001)) cf_8_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_8));
    wire cf_9 = 1'b1;
    wire cf_10;
    case4_lut4 #(.LUT(16'he0e7)) cf_10_cell (.A({datain[4],datain[3],datain[2],datain[1]}), .Y(cf_10));
    wire cf_11;
    case4_lut4 #(.LUT(16'h70f1)) cf_11_cell (.A({datain[4],datain[3],datain[2],datain[1]}), .Y(cf_11));
    wire cf_12;
    case4_lut4 #(.LUT(16'h0116)) cf_12_cell (.A({datain[4],datain[3],datain[1],datain[0]}), .Y(cf_12));
    wire cf_13;
    case4_lut4 #(.LUT(16'hff7e)) cf_13_cell (.A({datain[4],datain[3],datain[1],datain[0]}), .Y(cf_13));
    wire cf_14;
    case4_lut4 #(.LUT(16'hcdda)) cf_14_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_14));
    wire cf_15;
    case4_lut4 #(.LUT(16'h4dcd)) cf_15_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_15));
    wire cf_16;
    case4_lut4 #(.LUT(16'h2bbd)) cf_16_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_16));
    wire cf_17;
    case4_lut4 #(.LUT(16'habaa)) cf_17_cell (.A({datain[3],datain[2],datain[1],datain[0]}), .Y(cf_17));
    assign lookup_word[0] = datain[3] ? cf_1 : cf_0;
    assign lookup_word[1] = datain[3] ? cf_0 : cf_2;
    assign lookup_word[2] = datain[1] ? cf_3 : cf_0;
    assign lookup_word[3] = datain[4] ? cf_5 : cf_4;
    assign lookup_word[4] = datain[4] ? cf_7 : cf_6;
    assign lookup_word[5] = datain[4] ? cf_9 : cf_8;
    assign lookup_word[6] = datain[0] ? cf_11 : cf_10;
    assign lookup_word[7] = datain[2] ? cf_13 : cf_12;
    assign lookup_word[8] = datain[4] ? cf_15 : cf_14;
    assign lookup_word[9] = datain[4] ? cf_17 : cf_16;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            dataout <= 6'b000000;
            only_5b <= 1'b1;
            data_dp <= 1'b0;
            data_dn <= 1'b0;
            // Deliberately no assignment to datain_5bis7: preserve hold semantics.
        end else if (!ena) begin
            {dataout, only_5b, datain_5bis7, data_dp, data_dn} <= 10'b0;
        end else begin
            {dataout, only_5b, datain_5bis7, data_dp, data_dn} <= lookup_word;
        end
    end
endmodule
