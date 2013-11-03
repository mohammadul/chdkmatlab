function makeall()
f = dir();
l = length(f);
for i = 1:l
    if(~f(i).isdir || f(i).name(1)=='.')
        continue;
    end
    try
        run([f(i).name '/make.m']);
    catch me
    end
end

end
