pp = [0 0 0.015 0.18 0.7 0.96 0.7 0.18 0.015 0 0];
t = -2.5:0.5:2.5;

v = -0.000:-0.001:-1.999;


for ix1 = 1:length(v),
	    disp(ix1);
 for ix2 = 1:length(v),
  p = exp(v(ix1)*t.^2+v(ix2)*t.^4);
  r(ix1,ix2) = norm(p./max(abs(p)) - pp./max(abs(pp)));
 end;
end;


