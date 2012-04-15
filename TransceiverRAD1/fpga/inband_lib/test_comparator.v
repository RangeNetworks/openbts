// -*- verilog -*-
// Range Networks 
// Unsigned 16-bit greater or equal comparator.
// for test module

module testcompar
   (
     input usbdata_packed[31:0],
	  output reg test_bit1
	);
	usbdata_packed[15:0] A;
	usbdata_packed[31:16] B;
	assign test_bit1 = A == B ? 1'b1 : 1'b0;
	endmodule 