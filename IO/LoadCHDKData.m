%LOADCHDKDATA - Imports CHDK RAW Data
%   im = LoadCHDKData(fname, input_bpp, size)
%       fname - input CHDK raw image filename
%       input_bpp - input raw bpp (0 - 3)
%           0 - 8 bits per pixel
%           1 - 10 bits per pixel (default)
%           2 - 12 bits per pixel
%           3 - 14 bits per pixel
%       size - raw image size ([height; width])
%       im - output raw image
%
% Author: Sk. Mohammadul Haque
% Copyright (c) 2013 Sk. Mohammadul Haque
% Website: http://mohammadulhaque.alotspace.com
%