# GrabAccess

GrabAccess是用于绕过Windows登录密码和Bitlocker的工具。 

在物理接触的条件下，可以植入Shift键后门或指定的木马程序。

## 快速开始

GrabAccess最基础的功能是安装Shift键后门，将Windows粘滞键替换为任务管理器。在未登录的情况下，攻击者可以借此执行系统命令或读写文件。

1. 准备一个U盘。如果是FAT16或FAT32格式，则可以不删除已有的文件。否则需要格式化为上述格式。

2. 下载[GrabAccess_Release.zip](https://github.com/Push3AX/GrabAccess/releases/download/Release/GrabAccess_Release.zip)，解压，将其中的EFI文件夹拷贝到U盘根目录。

   ![image-20220826174606382](https://raw.githubusercontent.com/Push3AX/GrabAccess/main/images/1.png)

3. 将U盘插入电脑，重启。在启动时进入BIOS菜单。

4. 选择从U盘启动，或设置U盘为第一启动项（如果BIOS开启了Security Boot，还需将其设置为DISABLE）.

   ![2](https://raw.githubusercontent.com/Push3AX/GrabAccess/main/images/2.png)

5. 如果在Windows引导阶段，出现了伪造的CHKDSK界面，说明后门已经植入成功。

   ![3](https://raw.githubusercontent.com/Push3AX/GrabAccess/main/images/3.png)

6. 在登录界面，连续按下五次Shift键，即可唤出任务管理器。

   ![4](https://raw.githubusercontent.com/Push3AX/GrabAccess/main/images/4.png)

## 自动化植入木马

GrabAccess提供了自动化植入木马程序的功能，并且会自动将其添加到启动项。

要使用该功能，需要预先将GrabAccess与要植入的程序打包：

1. 下载[GrabAccess_Release.zip](https://github.com/Push3AX/GrabAccess/releases/download/Release/GrabAccess_Release.zip)，解压。
2. 将需要植入的程序，命名为payload.exe，替换目录下的同名文件。
3. 运行build.bat。
4. 将EFI文件夹复制到U盘根目录。
     （U盘如果是FAT16或FAT32格式，则可以不删除已有的文件。否则需要格式化为上述格式。）
5. 将U盘插入电脑，重启电脑。在启动时进入BIOS菜单。
6. 选择从U盘启动，或设置U盘为第一启动项（如果BIOS开启了Security Boot，还需将其设置为DISABLE）.
7. 如果在Windows引导阶段，出现了伪造的CHKDSK界面，说明后门已经植入成功。
8. 在登陆系统后，payload.exe将会自动启动。

## 绕过Bitlocker

GrabAccess的自动化植入功能，可以绕过Bitlocker的系统盘加密。

当从含有GrabAccess的U盘启动后，GrabAccess的应用程序就会写入到内存中。此时可以拔出U盘，但需要停留在Bitlocker输入密码的界面，等待受害者返回输入密码。不可直接关机或重启。

![5](https://raw.githubusercontent.com/Push3AX/GrabAccess/main/images/5.png)

因为只有Bitlocker解锁之后，GrabAccess的应用程序才会被写入磁盘并执行。在这之前，GrabAccess仅仅临时存在于内存之中。如果在受害者登录之前重启或关机，GrabAccess将会失效。

## Credits

1. [Windows Platform Binary Table (WPBT) ](https://download.microsoft.com/download/8/a/2/8a2fb72d-9b96-4e2d-a559-4a27cf905a80/windows-platform-binary-table.docx)
2. [Grub2 by a1ive](https://github.com/a1ive/grub)
3. [Windows Native App by Fox](http://fox28813018.blogspot.com/2019/05/windows-platform-binary-table-wpbt-wpbt.html)

------

# 解析

## Windows Platform Binary Table

​	和Kon-boot篡改Windows内核不同，GrabAccess的工作原理，源自于Windows的一项合法后门：WPBT（Windows Platform Binary Table）。

​	WPBT常用于计算机制造商植入驱动管理软件、防丢软件。类似Bootkit病毒，一旦主板中存在WPBT条目，无论是重装系统还是更换硬盘，只要使用Windows系统，开机后都会被安装指定程序。

​    WPBT的原始设计，应当是由生产商在主板的UEFI固件中插入一个特定的模块实现。但是，通过劫持UEFI的引导过程，攻击者可以插入WPBT条目，而无需修改主板固件。

## GrabAccess做了什么

​	 GrabAccess包含两个部分。

​	其一是用于写入WPBT条目的UEFI应用程序，即源码中的Grab2。是一个修改过的Grab2，包含用于加载WPBT的模块。

​	其二是一个Windows Native Application，即源码中的NativeAPP。负责写出文件、添加启动项、劫持Windows粘滞键。

​	在GrabAccess启动之后，Grab2会向内存中写入WPBT条目。接下来Windows正常引导。并加载和执行Native APP。

## Native APP做了什么

​	WPBT所加载的应用程序，并非常规的Win32程序。而是一个Windows Native Application。它在Windows引导过程中执行，早于用户登录。但是Windows提供给Native APP的API，也少于Win32程序。	

​	GrabAccess_Release_v1/bin/nativex64.exe是一个Windows Native App，在Windows引导过程中运行。首先将其末尾的程序写入到：

```
C:\Windows\System32\NTUpdateService.exe
```

​	后添加注册表项目：

```
\Registry\Machine\SOFTWARE\Microsoft\Windows\CurrentVersion\Run
\\Registry\Machine\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\sethc.exe
```

​	实现自启动，以及劫持Shift键。

## 绕过签名校验

​	Windows在加载Native APP时，会对其进行签名校验。但该过程不会验证签名的有效期，以及签名是否已经被吊销（CVE-2022-21836）。

​	GrabAccess使用了Hacking Team泄漏，并已经被吊销的数字签名，对Native APP进行签名。

​	Windows对该漏洞的修复方法，是在driver.stl中添加了一个黑名单，禁止一系列泄漏的数字签名。未来Hacking Team的签名可能会失效，需要更新为其它泄漏的签名。

## 二次开发

​    GrabAccess是对WPBT武器化利用的一个开源实现。开源协议为GPL。读者可以按照需求二次开发，尤其是通过修改其中的Native APP。

​	目前写入启动项和劫持Shift键的实现，只使用了最基础的手段。如有需要，可以自行编写更高级和隐蔽的后渗透技巧。

​	源码中的Grab2，需要在Linux环境下编译。理论上BuildGrabAccess.sh脚本可以完成环境的配置和编译。

​	源码中的Windows Native APP。对其编译需要配置Windows Driver Kits环境。环境配置完成后，在x64 Free Build Environment中运行Build64.bat，即可完成编译。