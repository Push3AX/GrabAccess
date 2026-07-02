# GrabAccess Stage1-UEFI

`Stage1-UEFI` 是GrabAccess的第一部分，运行于UEFI阶段。负责在计算机启动之初、Windows Boot Manager接管之前把Stage2-Native NT程序注册到Windows Platform Binary Table (WPBT)。

本目录包含两个形式的模块：

1. `GrabAccess`：UEFI Application，用于 U 盘启动场景。
2. `GrabAccessDXE`：DXE Driver，用于主板固件集成场景。

二者最终做的事情相同：取得 Stage2 的 `native.exe` 内容，把它放入 ACPI Reclaim memory，并构造 WPBT 表。U 盘版本从文件系统读取 `native.exe`，DXE 版本从固件 FV 的 raw section 读取 `native.exe`。



## GrabAccess.efi

`GrabAccess`是U盘启动时使用的UEFI应用程序。执行流程：

1. 从含有GrabAccess的U盘引导系统启动。
2. GrabAccess从U盘的文件系统根目录打开 `native.exe`。
3. 读取文件大小，将内容读入 `EfiACPIReclaimMemory`。
4. 构造 WPBT 表：
   - `BinarySize` 指向 `native.exe` 大小。
   - `BinaryLocation` 指向已读入内存的 Stage2。
   - `Layout = 0x01`
   - `Type = 0x01`
   - `ArgLength` 当前为空字符串长度。
5. 通过 `EFI_ACPI_TABLE_PROTOCOL.InstallAcpiTable` 安装 WPBT。
6. 枚举文件系统，寻找 `\EFI\Microsoft\Boot\bootmgfw.efi`。
7. 调用 `LoadImage` / `StartImage` 继续启动 Windows Boot Manager。



## GrabAccessDXE

`GrabAccessDXE` 是植入主板固件时使用的 DXE Driver。执行流程：

1. GrabAccessDXE被植入到主板UEFI固件之后，系统启动。
2. 在 DXE 阶段注册 `gEfiEventReadyToBootGuid` 事件。
3. ReadyToBoot 触发时调用 `InstallWpbt`。
4. 通过 `EFI_FIRMWARE_VOLUME2_PROTOCOL` 枚举固件 volume。
5. 查找 GUID 为 `2136252F-5F7C-486D-B89F-545EC42AD45C` 的 raw section。
6. 将该 raw section 作为 Stage2 payload 读入 `EfiACPIReclaimMemory`。
7. 构造并安装 WPBT。
8. 关闭 ReadyToBoot 事件，避免重复安装。

DXE 版本不从磁盘文件系统读取 `native.exe`，而是从固件 FV 中读取指定 GUID 的 raw section。



## 编译GrabAccess Stage1

要编译GrabAccess Stage1，需要配置EDK2环境。注意版本必须为edk2-stable202002，更高版本无法通过编译。

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

如果编译成功，可以使用QEMU运行编译出来的UEFI固件。

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

如果希望测试它，可以通过以下步骤在QEMU中安装Windows：

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

最后，换用前文中编译的 OVMF 固件镜像来启动 Windows：

```bash
qemu-system-x86_64 -m 4096 -boot d -enable-kvm -smp 4 -net nic -net user -hda windows10.qcow2 -usb -device usb-tablet -drive file=./OVMF.fd,format=raw,if=pflash -serial file:debug.log
```

在生成的debug.log中，可以看到UEFI的调试信息，包括GrabAccess的输出。同时，在Windows启动之后，可以看到GrabAccess的效果。

------

# GrabAccess Stage1-UEFI

`Stage1-UEFI` is the first part of GrabAccess and runs in the UEFI phase. Its job is to register the Stage2 Native NT program in the Windows Platform Binary Table (WPBT) before Windows Boot Manager takes control.

This directory contains two module types:

1. `GrabAccess`: a UEFI Application for USB boot scenarios.
2. `GrabAccessDXE`: a DXE Driver for motherboard firmware integration scenarios.

Both modules ultimately do the same thing: obtain the Stage2 `native.exe` content, place it in ACPI Reclaim memory, and construct a WPBT table. The USB version reads `native.exe` from the filesystem, while the DXE version reads the same type of payload from a raw section in the firmware volume.



## GrabAccess.efi

`GrabAccess` is the UEFI application used for USB boot. Execution flow:

1. Boot the system from the USB drive that contains GrabAccess.
2. GrabAccess opens `native.exe` from the root of the USB filesystem.
3. It reads the file size and loads the content into `EfiACPIReclaimMemory`.
4. It constructs the WPBT table:
   - `BinarySize` points to the size of `native.exe`.
   - `BinaryLocation` points to the Stage2 content loaded in memory.
   - `Layout = 0x01`
   - `Type = 0x01`
   - `ArgLength` is currently the length of an empty string.
5. It installs WPBT through `EFI_ACPI_TABLE_PROTOCOL.InstallAcpiTable`.
6. It enumerates filesystems and looks for `\EFI\Microsoft\Boot\bootmgfw.efi`.
7. It calls `LoadImage` / `StartImage` to continue booting Windows Boot Manager.



## GrabAccessDXE

`GrabAccessDXE` is the DXE Driver used when integrating GrabAccess into motherboard firmware. Execution flow:

1. The system starts after GrabAccessDXE has been inserted into the motherboard UEFI firmware.
2. During the DXE phase, it registers a `gEfiEventReadyToBootGuid` event.
3. When ReadyToBoot fires, it calls `InstallWpbt`.
4. It enumerates firmware volumes through `EFI_FIRMWARE_VOLUME2_PROTOCOL`.
5. It looks for a raw section with GUID `2136252F-5F7C-486D-B89F-545EC42AD45C`.
6. It reads that raw section as the Stage2 payload into `EfiACPIReclaimMemory`.
7. It constructs and installs WPBT.
8. It closes the ReadyToBoot event to avoid repeated installation.

The DXE version does not read `native.exe` from the disk filesystem. Instead, it reads the raw section with the specified GUID from the firmware volume.



## Building GrabAccess Stage1

To build GrabAccess Stage1, configure an EDK2 environment. One possible setup is:

```bash
# On Ubuntu 18
sudo apt install build-essential uuid-dev nasm iasl git gcc-5 g++-5 gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf python3-distutils
git clone https://github.com/tianocore/edk2.git
cd edk2
git checkout tags/edk2-stable202002 -b edk2-stable202002
git submodule update --init
make -C BaseTools
source edksetup.sh
build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc -D DEBUG_ON_SERIAL_PORT
```

If the build succeeds, you can run the generated UEFI firmware with QEMU.

```bash
sudo apt install qemu qemu-kvm libvirt-bin bridge-utils virt-manager ovmf
sudo systemctl start libvirtd
sudo systemctl enable libvirtd
qemu-system-x86_64 -bios Build/OvmfX64/DEBUG_GCC5/FV/OVMF.fd
```

After the EDK2 environment is ready, copy the `GrabAccess` and `GrabAccessDXE` directories into `edk2/OvmfPkg`, and replace the original `OvmfPkgX64.dsc` and `OvmfPkgX64.fdf` with the versions from this directory.

Then run:

```bash
build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc -D DEBUG_ON_SERIAL_PORT
```

The generated UEFI application is located at:

```text
edk2/Build/OvmfX64/DEBUG_GCC5/X64/OvmfPkg/GrabAccess/GrabAccess/OUTPUT/GrabAccess.efi
```

The build also generates an OVMF firmware image that contains GrabAccessDXE:

```text
edk2/Build/OvmfX64/DEBUG_GCC5/FV/OVMF.fd
```

This image can be used for QEMU debugging.

To test it, first install Windows in QEMU.

Create a virtual disk:

```bash
mkdir WPBT
cd WPBT
qemu-img create -f qcow2 windows10.qcow2 64G
```

Prepare a Windows 10 x64 ISO. This example uses `windows10_x64_1909.iso`. Start the VM and install Windows:

```bash
qemu-system-x86_64 -m 4096 -boot d -enable-kvm -smp 4 -net nic -net user -usb -device usb-tablet -hda windows10.qcow2 -cdrom ./windows10_x64_1909.iso -bios /usr/share/OVMF/OVMF_CODE.fd
```

Finally, boot Windows with the OVMF firmware image built earlier:

```bash
qemu-system-x86_64 -m 4096 -boot d -enable-kvm -smp 4 -net nic -net user -hda windows10.qcow2 -usb -device usb-tablet -drive file=./OVMF.fd,format=raw,if=pflash -serial file:debug.log
```

The generated `debug.log` contains UEFI debug output, including GrabAccess messages. After Windows starts, you should be able to observe the GrabAccess effect.
