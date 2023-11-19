# GrabAccess_Stage 2

在Stage 1结束后，WPBT条目被写入内存，同时Windows启动。在启动过程中，smss.exe会加载内存中WPBT表内的Native NT应用程序并执行，也就是本目录下的程序。

要编译这个程序，需要安装Windows Driver Kits环境，配置完成后，在x64 Free Build Environment中运行BuildNativeAPP.bat，即可完成编译。



Upon the completion of Stage 1, the WPBT entry is written into memory, and Windows starts. During the startup process, `smss.exe` will load and execute the Native NT application from the WPBT table in memory, which is the program in this directory.

To compile this program, you need to install the Windows Driver Kits (WDK) environment. Once set up, run `BuildNativeAPP.bat` in the x64 Free Build Environment to compile.

