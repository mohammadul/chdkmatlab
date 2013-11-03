function make()
disp('Compiling...');
mex LoadCHDKData.cpp -largeArrayDims
disp('Done.');
end