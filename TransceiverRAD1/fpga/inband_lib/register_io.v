module register_io
	(clk, reset, enable, addr, datain, dataout, debugbus, addr_wr, data_wr, strobe_wr,
	 rssi_0, rssi_1, rssi_2, rssi_3, threshhold, rssi_wait, reg_0, reg_1, reg_2, reg_3, 
     atr_tx_delay, atr_rx_delay, master_controls, debug_en, interp_rate, decim_rate, 
     atr_mask_0, atr_txval_0, atr_rxval_0, atr_mask_1, atr_txval_1, atr_rxval_1,
     atr_mask_2, atr_txval_2, atr_rxval_2, atr_mask_3, atr_txval_3, atr_rxval_3, 
     txa_refclk, txb_refclk, rxa_refclk, rxb_refclk, misc, txmux);   
	
	input clk;
	input reset;
	input wire [1:0] enable;
	input wire [6:0] addr; 
	input wire [31:0] datain;
	output reg [31:0] dataout;
	output wire [15:0] debugbus;
	output reg [6:0] addr_wr;
	output reg [31:0] data_wr;
	output wire strobe_wr; 
	input wire [31:0] rssi_0;
	input wire [31:0] rssi_1;
	input wire [31:0] rssi_2; 
	input wire [31:0] rssi_3; 
	output wire [31:0] threshhold;
	output wire [31:0] rssi_wait;
	input wire [15:0] reg_0;
	input wire [15:0] reg_1; 
	input wire [15:0] reg_2; 
	input wire [15:0] reg_3;
	input wire [11:0] atr_tx_delay;
	input wire [11:0] atr_rx_delay;
	input wire [7:0]  master_controls;
	input wire [3:0]  debug_en;
	input wire [15:0] atr_mask_0;
	input wire [15:0] atr_txval_0;
	input wire [15:0] atr_rxval_0;
	input wire [15:0] atr_mask_1;
	input wire [15:0] atr_txval_1;
	input wire [15:0] atr_rxval_1;
	input wire [15:0] atr_mask_2;
	input wire [15:0] atr_txval_2;
	input wire [15:0] atr_rxval_2;
	input wire [15:0] atr_mask_3;
	input wire [15:0] atr_txval_3;
	input wire [15:0] atr_rxval_3;
	input wire [7:0]  txa_refclk;
	input wire [7:0]  txb_refclk;
	input wire [7:0]  rxa_refclk;
	input wire [7:0]  rxb_refclk;
	input wire [7:0]  interp_rate;
	input wire [7:0]  decim_rate;
	input wire [7:0]  misc;
	input wire [31:0] txmux;
	
	wire [31:0] bundle[43:0]; 
   assign bundle[0] = 32'hFFFFFFFF;
   assign bundle[1] = 32'hFFFFFFFF;
   assign bundle[2] = {20'd0, atr_tx_delay};
   assign bundle[3] = {20'd0, atr_rx_delay};
   assign bundle[4] = {24'sd0, master_controls};
   assign bundle[5] = 32'hFFFFFFFF;
   assign bundle[6] = 32'hFFFFFFFF;
   assign bundle[7] = 32'hFFFFFFFF;
   assign bundle[8] = 32'hFFFFFFFF;
   assign bundle[9] = {15'd0, reg_0};
   assign bundle[10] = {15'd0, reg_1};
   assign bundle[11] = {15'd0, reg_2};
   assign bundle[12] = {15'd0, reg_3};
   assign bundle[13] = {15'd0, misc};
   assign bundle[14] = {28'd0, debug_en};
   assign bundle[15] = 32'hFFFFFFFF;
   assign bundle[16] = 32'hFFFFFFFF;
   assign bundle[17] = 32'hFFFFFFFF;
   assign bundle[18] = 32'hFFFFFFFF;
   assign bundle[19] = 32'hFFFFFFFF;
   assign bundle[20] = {16'd0, atr_mask_0};
   assign bundle[21] = {16'd0, atr_txval_0};
   assign bundle[22] = {16'd0, atr_rxval_0};
   assign bundle[23] = {16'd0, atr_mask_1};
   assign bundle[24] = {16'd0, atr_txval_1};
   assign bundle[25] = {16'd0, atr_rxval_1};
   assign bundle[26] = {16'd0, atr_mask_2};
   assign bundle[27] = {16'd0, atr_txval_2};
   assign bundle[28] = {16'd0, atr_rxval_2};
   assign bundle[29] = {16'd0, atr_mask_3};
   assign bundle[30] = {16'd0, atr_txval_3};
   assign bundle[31] = {16'd0, atr_rxval_3};
   assign bundle[32] = {24'd0, interp_rate};
   assign bundle[33] = {24'd0, decim_rate};
   assign bundle[34] = 32'hFFFFFFFF;
   assign bundle[35] = 32'hFFFFFFFF;
   assign bundle[36] = 32'hFFFFFFFF;
   assign bundle[37] = 32'hFFFFFFFF;
   assign bundle[38] = 32'hFFFFFFFF;
   assign bundle[39] = txmux;
   assign bundle[40] = {24'd0, txa_refclk};
   assign bundle[41] = {24'd0, rxa_refclk};
   assign bundle[42] = {24'd0, txb_refclk};
   assign bundle[43] = {24'd0, rxb_refclk};  

	reg strobe;
	wire [31:0] out[7:0];
	assign debugbus = {clk, enable, addr[2:0], datain[4:0], dataout[4:0]};
	assign threshhold = out[1];
	assign rssi_wait = out[2];
	assign strobe_wr = strobe;
	
	always @(*)
        if (reset | ~enable[1])
           begin
             strobe <= 0;
		     dataout <= 0;
		   end
		else
		   begin
	         if (enable[0])
	           begin
	             //read
				if (addr <= 7'd43)
					dataout <= bundle[addr];
				else if (addr <= 7'd57 && addr >= 7'd50)
					dataout <= out[addr-7'd50];
				else
					dataout <= 32'hFFFFFFFF; 	
	            strobe <= 0;
              end
             else
               begin
                 //write
	             dataout <= dataout;
                 strobe <= 1;
				 data_wr <= datain;
				 addr_wr <= addr;
               end
          end

//register declarations
    setting_reg #(50) setting_reg0(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[0]));
    setting_reg #(51) setting_reg1(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[1]));
    setting_reg #(52) setting_reg2(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[2]));
    setting_reg #(53) setting_reg3(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[3]));
    setting_reg #(54) setting_reg4(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[4]));
    setting_reg #(55) setting_reg5(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[5]));
    setting_reg #(56) setting_reg6(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[6]));
    setting_reg #(57) setting_reg7(.clock(clk),.reset(reset),
    .strobe(strobe_wr),.addr(addr_wr),.in(data_wr),.out(out[7]));
endmodule	
