cd ..
del /s /q build64
mkdir build64
cd build64
cmake -G "Visual Studio 12 Win64" -DCMAKE_INSTALL_PREFIX=../bin/win8-64 ../neo
pause