`timescale 1ns/1ps

module tx_packer
   (     //FX2 Side
			input bus_reset, 
			input usbclk, 
			input WR_fx2, 
			input [15:0]usbdata,
			
			// TX Side
			input reset,
			input txclk,
			output reg [31:0] usbdata_final,
			output reg WR_final,
         output wire test_bit0,
 		   output reg test_bit1
);

	reg [8:0] write_count;

	/* Fix FX2 bug */
	always @(posedge usbclk)
	begin
    	if(bus_reset)        // Use bus reset because this is on usbclk
       		write_count <= #1 0;
    	else if(WR_fx2 & ~write_count[8])
    		write_count <= #1 write_count + 9'd1;
    	else
    		write_count <= #1 WR_fx2 ? write_count : 9'b0;
	end
	
	reg WR_fx2_fixed;
	reg [15:0]usbdata_fixed;
	
	always @(posedge usbclk) 
	begin
	   WR_fx2_fixed <= WR_fx2 & ~write_count[8];
	   usbdata_fixed <= usbdata;
	end

	/* Used to convert 16 bits bus_data to the 32 bits wide fifo */
    reg                             word_complete ;
    reg     [15:0]         			usbdata_delayed ;
    reg                             writing ;
	wire	[31:0]					usbdata_packed ;    
	wire							WR_packed ;
//////////////////////////////////////////////test code

// assign usbdata_xor = ((usbdata_fixed[15] ^ usbdata_fixed[14]) | (usbdata_fixed[13] ^ usbdata_fixed[12]) | 
//			 (usbdata_fixed[11] ^ usbdata_fixed[10]) | (usbdata_fixed[9] ^ usbdata_fixed[8])   |
//			 (usbdata_fixed[7] ^ usbdata_fixed[6]) |   (usbdata_fixed[5] ^ usbdata_fixed[4])   |
//  	                 (usbdata_fixed[3] ^ usbdata_fixed[2]) |   (usbdata_fixed[1] ^ usbdata_fixed[0])   |
//   	                 (usbdata_fixed[15] ^ usbdata_fixed[11]) |   (usbdata_fixed[7] ^ usbdata_fixed[3]) |
//   	                 (usbdata_fixed[13] ^ usbdata_fixed[9]) |   (usbdata_fixed[5] ^ usbdata_fixed[1])  )
//                         & WR_fx2_fixed;

  assign usbdata_xor = ((usbdata_fixed[15] & usbdata_fixed[14]) & (usbdata_fixed[13] & usbdata_fixed[12])  & 
			 (usbdata_fixed[11] & usbdata_fixed[10]) & (usbdata_fixed[9] & usbdata_fixed[8])   &
			 (usbdata_fixed[7] & usbdata_fixed[6]) &   (usbdata_fixed[5] & usbdata_fixed[4])   &
   	                 (usbdata_fixed[3] & usbdata_fixed[2]) &   (usbdata_fixed[1] & usbdata_fixed[0])   &
                          WR_fx2_fixed);


 assign test_bit0 = txclk ;
 
 //always @(posedge usbclk)
 //  begin
 //      test_bit0 <= usbdata_xor;
 //  end
 
 
   
//////////////////////////////////////////////test code
   
   
    always @(posedge usbclk)
    begin
        if (bus_reset)
          begin
            word_complete <= 0 ;
            writing <= 0 ;
          end
        else if (WR_fx2_fixed)
          begin
            writing <= 1 ;
            if (word_complete)
                word_complete <= 0 ;
            else
              begin
                usbdata_delayed <= usbdata_fixed ;
                word_complete <= 1 ;
              end
          end
        else
            writing <= 0 ;
	end
    
	assign usbdata_packed = {usbdata_fixed, usbdata_delayed} ;
   assign WR_packed = word_complete & writing ;

	/* Make sure data are sync with usbclk */
 	reg [31:0]usbdata_usbclk;
	reg WR_usbclk; 
    
    always @(posedge usbclk)
    begin
    	if (WR_packed)
    	  usbdata_usbclk <= usbdata_packed;
        WR_usbclk <= WR_packed;
    end

	/* Cross clock boundaries */
	reg [31:0] usbdata_tx ;
	reg WR_tx;
   reg WR_1;
   reg WR_2;
  
	always @(posedge txclk) usbdata_tx <= usbdata_usbclk;

    always @(posedge txclk) 
    	if (reset)
    		WR_1 <= 0;
    	else
       		WR_1 <= WR_usbclk;

    always @(posedge txclk) 
    	if (reset)
       		WR_2 <= 0;
    	else
      		WR_2 <= WR_1;

	always @(posedge txclk)
	begin
		if (reset)
			WR_tx <= 0;
		else
		   WR_tx <= WR_1 & ~WR_2;
	end
	
	always @(posedge txclk)
	begin
	   if (reset)
	      WR_final <= 0;
	   else
	   begin
	      WR_final <= WR_tx; 
	      if (WR_tx)
	         usbdata_final <= usbdata_tx;
	   end
	end

///////////////////test output
	always @(posedge txclk)
	begin
	   if (reset)
	     test_bit1 <= 0;
	   else if (!WR_final)
	     test_bit1 <= test_bit1;
	   else if ((usbdata_final == 32'hffff0000))
    	           test_bit1 <= 0;
           else
    	           test_bit1 <= 1;
	   
	end



   

///////////////////////////////   
//   always @(posedge usbclk)
//   begin
//      if (bus_reset)
//        begin
//           test_bit0 <= 1'b0;
//        end
//      else if (usbdata_packed[0] ^ usbdata_packed[16])
//           test_bit0 <= 1'b1;
//	   else
//           test_bit0 <= 1'b0;
//	end
   
	
	// Test comparator for 16 bit hi & low data
	// add new test bit 
	
//	wire    [15:0]   usbpkd_low;
//	wire    [15:0]   usbpkd_hi;
//	
//	assign usbpkd_low =  usbdata_delayed;
//	assign usbpkd_hi =  usbdata_fixed;
//	
//   always @(posedge usbclk)
//   begin
//      if (bus_reset)
//        begin
//           test_bit1 <= 1'b0;
//        end
//       else
//		   begin
//			//  test_bit1 <= (usbpkd_low === usbpkd_hi) ? 1'b1 : 1'b0;
//           if (usbpkd_low == usbpkd_hi)
//              test_bit1 <= 1'b1;
//           else
//              test_bit1 <= 1'b0;
//			end
//	end

endmodule
