%
% Laurent decomposition of GMSK signals
% Generates C0, C1, and C2 pulse shapes
%
% Pierre Laurent, "Exact and Approximate Construction of Digital Phase
%   Modulations by Superposition of Amplitude Modulated Pulses", IEEE
%   Transactions of Communications, Vol. 34, No. 2, Feb 1986.
%
% Author: Thomas Tsou <tom@tsou.cc>
%

% Modulation parameters
oversamp = 16;
L = 3;
f = 270.83333e3;
T = 1/f;
h = 0.5;
BT = 0.30;
B = BT / T;

% Generate sampling points for L symbol periods
t = -(L*T/2):T/oversamp:(L*T/2);
t = t(1:end-1) + (T/oversamp/2);

% Generate Gaussian pulse
g = qfunc(2*pi*B*(t - T/2)/(log(2)^.5)) - qfunc(2*pi*B*(t + T/2)/(log(2)^.5));
g = g / sum(g) * pi/2;
g = [0 g];

% Integrate phase 
q = 0;
for i = 1:size(g,2);
    q(i) = sum(g(1:i));
end

% Compute two sided "generalized phase pulse" function
s = 0;
for i = 1:size(g,2);
    s(i) = sin(q(i)) / sin(pi*h);
end
for i = (size(g,2) + 1):(2 * size(g,2) - 1);
    s(i) = sin(pi*h - q(i - (size(g,2) - 1))) / sin(pi*h);
end

% Compute C0 pulse: valid for all L values
c0 = s(1:end-(oversamp*(L-1)));
for i = 1:L-1;
    c0 = c0 .* s((1 + i*oversamp):end-(oversamp*(L - 1 - i)));
end

% Compute C1 pulse: valid for L = 3 only!
%   C1 = S0 * S4 * S2
c1 = s(1:end-(oversamp*(4)));
c1 = c1 .* s((1 + 4*oversamp):end-(oversamp*(4 - 1 - 3)));
c1 = c1 .* s((1 + 2*oversamp):end-(oversamp*(4 - 1 - 1)));

% Compute C2 pulse: valid for L = 3 only!
%   C2 = S0 * S1 * S5
c2 = s(1:end-(oversamp*(5)));
c2 = c2 .* s((1 + 1*oversamp):end-(oversamp*(5 - 1 - 0)));
c2 = c2 .* s((1 + 5*oversamp):end-(oversamp*(5 - 1 - 4)));

% Plot C0, C1, C2 Laurent pulse series
figure(1);
hold off;
plot((0:size(c0,2)-1)/oversamp - 2,c0, 'b');
hold on;
plot((0:size(c1,2)-1)/oversamp - 2,c1, 'r');
plot((0:size(c2,2)-1)/oversamp - 2,c2, 'g');

% Generate OpenBTS pulse
numSamples = size(c0,2); 
centerPoint = (numSamples - 1)/2;
i = ((0:numSamples) - centerPoint) / oversamp;
xP = .96*exp(-1.1380*i.^2 - 0.527*i.^4);
xP = xP / max(xP) * max(c0);

% Plot C0 pulse compared to OpenBTS pulse
figure(2);
hold off;
plot((0:size(c0,2)-1)/oversamp, c0, 'b');
hold on;
plot((0:size(xP,2)-1)/oversamp, xP, 'r');
