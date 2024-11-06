cd ..
mkdir build_cppffmpeg
cd build_cppffmpeg
cmake %~dp0 -DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake
pause