# GrabAccess_Stage 1

## 背景知识：UEFI

研究Bootkit是在和计算机主板的固件打交道。而UEFI是现代计算机的主板固件程序。

你可能听说过BIOS，UEFI是BIOS的后继，它大幅地改进了BIOS的设计。虽然它们本质上不是同一个东西，但由于历史遗留原因，很多时候也将UEFI称为BIOS。

传统的BIOS是当年IBM工程师为IBM PC开发的，并没有考虑到扩展性和后续开发的问题，更没有应用程序的概念。

而UEFI的设计非常现代化，可以近似的看作是一个操作系统。UEFI中可以包含驱动、应用程序、二进制文件等。例如开机按下del键进入的UEFI界面，本质上是一个UEFI应用程序。

## 编译GrabAccess

GrabAccess的第一个阶段位于UEFI环境。对应不同用途，共有两个版本：

GrabAccess是UEFI应用程序，用于在U盘中启动。它会读取同目录下的payload.exe文件，构建WPBT表写入内存，然后搜寻Windows Bootloader进行启动。

GrabAccessDXE是一个DXE Driver，用于植入主板的UEFI固件。当计算机启动时，DXE Driver会自动启动，搜寻UEFI固件FV中特定GUID的文件（payload），读取并构建WPBT表写入内存。

要编译这两个程序，需要配置EDK2环境，可以遵照以下步骤：

```bash
#在Ubuntu18下
sudo apt install build-essential uuid-dev nasm iasl git gcc-5 g++-5 gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf python3-distutils
git clone https://github.com/tianocore/edk2.git
cd edk2
git checkout tags/edk2-stable202002 -b edk2-stable202002
git submodule update --init
make -C BaseTools
source edksetup.sh
build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc -D DEBUG_ON_SERIAL_PORT
```

此时如果编译成功，就可以使用QEMU运行编译出来的UEFI固件。

```bash
sudo apt install qemu qemu-kvm libvirt-bin bridge-utils virt-manager ovmf
sudo systemctl start libvirtd 
sudo systemctl enable libvirtd
qemu-system-x86_64 -bios Build/OvmfX64/DEBUG_GCC5/FV/OVMF.fd
```

在完成EDK2配置后，将`GrabAccess`和`GrabAccessDXE`文件夹复制到`edk2/OvmfPkg`目录下，并替换原有的`OvmfPkgX64.dsc`和`OvmfPkgX64.fdf`

最后运行`build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc -D DEBUG_ON_SERIAL_PORT`即可完成编译。

得到的文件位于`edk2/Build/OvmfX64/DEBUG_GCC5/X64/OvmfPkg/GrabAccess/GrabAccess/OUTPUT/GrabAccess.efi`

同时，会生成一份OVMF镜像`edk2/Build/OvmfX64/DEBUG_GCC5/FV/OVMF.fd`，其中包含GrabAccessDXE，可以使用QEMU对其进行调试。

如果你希望测试它，可以通过以下步骤在QEMU中安装Windows：

使用以下命令创建虚拟机的磁盘：

```bash
mkdir WPBT 
cd WPBT
qemu-img create -f qcow2 windows10.qcow2 64G
```

准备一份Windows 10 X64镜像(本例使用windows10_x64_1909.iso)。使用以下命令启动虚拟机，并安装Windows 10系统：

```bash
qemu-system-x86_64 -m 4096 -boot d -enable-kvm -smp 4 -net nic -net user -usb -device usb-tablet -hda windows10.qcow2 -cdrom ./windows10_x64_1909.iso -bios /usr/share/OVMF/OVMF_CODE.fd
```

最后，换用前文中编译的BIOS镜像来启动Windows：

```bash
qemu-system-x86_64 -m 4096 -boot d -enable-kvm -smp 4 -net nic -net user -hda windows10.qcow2 -usb -device usb-tablet -drive file=./OVMF.fd,format=raw,if=pflash -serial file:debug.log
```

在生成的debug.log中，可以看到UEFI的调试信息，包括GrabAccess的输出。同时，在Windows启动之后，可以看到GrabAccess的效果。
