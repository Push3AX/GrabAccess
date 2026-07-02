# GrabAccess Stage2-NativeNT

`Stage2-NativeNT` 是 GrabAccess 的第二阶段。Stage1 通过 WPBT 把打包后的 `native.exe` 交给 Windows，Windows 在早期启动阶段由 `smss.exe` 提取并执行它，运行时文件路径是：

```text
C:\Windows\System32\Wpbbin.exe
```

这是一个 Native NT 程序，不是普通 Win32 程序。它运行在正常用户会话启动之前，因此只能依赖 Native API 和少量早期可用的系统对象。



## 它会做什么

在 GrabAccess 的启动链路中，它负责承上启下：

1. `Stage1-UEFI` 安装 WPBT，让 Windows 在启动早期执行 Stage2。
2. `Stage2-NativeNT` 从自身文件中解析并释放打包进去的 payload。
3. 如果打包了自定义 `payload.exe`，Stage2 只安装用户 payload 及其自启动项。
4. 如果没有自定义 `payload.exe`，Stage2 释放 `Injector.exe`、`GrabAccessMsvpBypass.dll`、`GrabAccessExplorerHost.exe` 和 `GrabAccessFallback.exe`，并设置 LogonUI IFEO 劫持。
5. `Stage3-Injector` 负责把 Stage3 DLL 注入 `lsass.exe`。
6. `Stage3-MsvpBypass` 负责 patch 本地密码校验路径。
7. `Stage3-Fallback` 和 `Stage3-ExplorerHost` 负责不可绕过时的辅助界面和文件管理功能。

Stage2 主要负责 payload / Stage3 文件落地、判断能否绕过登录、写入 LogonUI IFEO、写入原因文件和清理脚本。真正的认证 patch 逻辑不在本目录，而在 `Stage3-MsvpBypass`。



## 打包和格式

`Release\build.ps1` 会把 `Release\bin\nativex64.exe` 打包成最终的 `Release\native.exe`。格式为：

```text
base native exe | payload0 | ... | payloadN | GA_PACKAGE_FOOTER
```

footer 使用 `GAPACK1!` magic、版本号、payload 数量，以及每个 payload 的 offset/size。当前格式最多支持 4 个 payload：自定义 payload 模式使用 1 个，默认绕过工具模式使用 4 个。



## 两种运行模式

### 自定义 payload 模式

如果 `Release\payload.exe` 存在，打包结果为：

```text
nativex64.exe | payload.exe | footer
```

Stage2 的行为是：

1. 从 `Wpbbin.exe` 中释放用户 payload。
2. 写入：

```text
C:\Windows\System32\GrabAccess.exe
```

3. 写入自启动注册表：

```text
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run
  GrabAccessAutorun = C:\Windows\System32\GrabAccess.exe
```

4. 回到正常启动流程。



### 默认绕过工具模式

如果 `Release\payload.exe` 不存在，打包结果为：

```text
nativex64.exe | Injector.exe | GrabAccessMsvpBypass.dll | GrabAccessExplorerHost.exe | GrabAccessFallback.exe | footer
```

Stage2 的行为是：

1. 释放：

```text
C:\Windows\System32\Injector.exe
C:\Windows\System32\GrabAccessMsvpBypass.dll
C:\Windows\System32\GrabAccessExplorerHost.exe
C:\Windows\System32\GrabAccessFallback.exe
```

2. 判断当前登录方式是否可以 patch。
3. 写入 `GrabAccessRestore.bat`、`GrabAccessCleanup.bat`、`GrabAccessReason.txt` 和 `GrabAccessMethod.txt`。
4. 设置 LogonUI 的 IFEO Debugger：

```text
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\LogonUI.exe
  Debugger = cmd.exe /c C:\Windows\System32\GrabAccessRestore.bat
```

5. 标记 `Wpbbin.exe` 自删除。

当 Windows 后续启动 LogonUI 时，IFEO 会先执行 `GrabAccessRestore.bat`。

如果登录方式可绕过，`GrabAccessRestore.bat` 会注入 `GrabAccessMsvpBypass.dll`，显示 6 秒提示，然后继续启动真正的 LogonUI。用户随后可以在密码输入框输入任意内容完成登录。

如果登录方式不可绕过，`GrabAccessRestore.bat` 会优先启动 `GrabAccessFallback.exe`。该界面会显示不可绕过原因，并提供三个入口：

- 管理文件：启动 `GrabAccessExplorerHost.exe`
- 命令行：启动原始 `cmd.exe`
- 管理账户：启动 `netplwiz.exe`

如果 `GrabAccessFallback.exe` 缺失或启动失败，则退回到最大化的 SYSTEM cmd。



## 哪些登录方式可以被绕过

当前对以下登录方式注入绕过 patch：

- 本地账号 + 密码
- 本地账号 + PIN
- 本地账号 + 图片密码

注入方式为：

```text
C:\Windows\System32\Injector.exe -n lsass.exe -i C:\Windows\System32\GrabAccessMsvpBypass.dll
```

用户在登录界面输入任意内容后，注入的 patch（Stage3 的 DLL）将会劫持登录认证，直接返回认证成功。



## 不能被绕过的登录方式

以下情况不会尝试 patch，而是在登录过程中打开 `GrabAccessFallback.exe`。只有 fallback 程序缺失或启动失败时，才会退回到 SYSTEM cmd：

- Microsoft Account
- Microsoft Entra ID / AzureAD 账号
- AD 域账号
- Hello 人脸
- Hello 指纹
- 智能卡、安全密钥或其它未支持 Credential Provider
- `RunAsPPL` / protected LSASS 已开启
- 账号来源或 Credential Provider 无法可靠识别



## 落地文件与清理

Stage2 自身会被 Windows 提取为：

```text
C:\Windows\System32\Wpbbin.exe
```

Stage2 结束前会通过 `FILE_DELETE_ON_CLOSE` 标记它自删除。

自定义 payload 模式会持久保留：

```text
C:\Windows\System32\GrabAccess.exe
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run\GrabAccessAutorun
```

默认绕过工具模式会临时写入：

```text
C:\Windows\System32\Injector.exe
C:\Windows\System32\GrabAccessMsvpBypass.dll
C:\Windows\System32\GrabAccessExplorerHost.exe
C:\Windows\System32\GrabAccessFallback.exe
C:\Windows\System32\GrabAccessRestore.bat
C:\Windows\System32\GrabAccessCleanup.bat
C:\Windows\System32\GrabAccessReason.txt
C:\Windows\System32\GrabAccessMethod.txt
C:\Windows\System32\GA_AccountSource.txt
C:\Windows\System32\GA_AuthSeen.flag
C:\Windows\System32\ga_status.txt
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\LogonUI.exe\Debugger
```

自动 patch 成功后，Stage3 会写出 `GA_AuthSeen.flag` 并启动 `GrabAccessCleanup.bat postauth`。cleanup 会等待认证完成和桌面出现，然后清理 LockApp / LogonUI 残留、IFEO、临时 BAT、Injector、Stage3 DLL、Fallback、ExplorerHost、flag、原因文件和调试日志。

不可绕过路径中，`GrabAccessFallback.exe` 退出时会启动 `GrabAccessCleanup.bat`，清理 IFEO 和临时文件。`GrabAccessExplorerHost.exe` 也会在退出时尝试删除自身。

`GrabAccessMsvpBypass.dll` 因为仍被进程占用而无法立即删除，cleanup 会通过 `MoveFileEx(..., MOVEFILE_DELAY_UNTIL_REBOOT)` 将它安排到下次重启时删除。



## 编译

优先使用仓库根目录的一键构建脚本：

```bat
make_all.bat --stage2-only
```

本目录下 `BuildNativeAPP.bat` 也可用于编译：

```bat
Stage2-NativeNT\BuildNativeAPP.bat
```

---



# GrabAccess Stage2-NativeNT

`Stage2-NativeNT` is the second stage of GrabAccess. Stage1 passes the packaged `native.exe` to Windows through WPBT. During early boot, Windows extracts and executes it through `smss.exe`. The runtime file path is:

```text
C:\Windows\System32\Wpbbin.exe
```

This is a Native NT program, not a normal Win32 program. It runs before a normal user session starts, so it can only rely on Native API calls and a small set of early-available system objects.



## What It Does

In the GrabAccess boot chain, this stage connects the earlier UEFI stage with the later Windows-side components:

1. `Stage1-UEFI` installs WPBT so Windows executes Stage2 during early boot.
2. `Stage2-NativeNT` parses itself and extracts the packaged payload.
3. If a custom `payload.exe` is packaged, Stage2 only installs the user payload and its autorun entry.
4. If no custom `payload.exe` is packaged, Stage2 extracts `Injector.exe`, `GrabAccessMsvpBypass.dll`, `GrabAccessExplorerHost.exe`, and `GrabAccessFallback.exe`, then sets a LogonUI IFEO hijack.
5. `Stage3-Injector` injects the Stage3 DLL into `lsass.exe`.
6. `Stage3-MsvpBypass` patches the local password validation path.
7. `Stage3-Fallback` and `Stage3-ExplorerHost` provide the fallback UI and file-management function when the current sign-in path cannot be bypassed.

Stage2 is mainly responsible for dropping the payload / Stage3 files, deciding whether the current sign-in path can be bypassed, writing the LogonUI IFEO entry, writing the reason files, and writing the cleanup script. The actual authentication patch logic is not in this directory; it is implemented in `Stage3-MsvpBypass`.



## Packaging And Format

`Release\build.ps1` packages `Release\bin\nativex64.exe` into the final `Release\native.exe`. The format is:

```text
base native exe | payload0 | ... | payloadN | GA_PACKAGE_FOOTER
```

The footer uses the `GAPACK1!` magic, a version number, the payload count, and each payload's offset/size. The current format supports up to 4 payloads: custom payload mode uses 1 payload, and default bypass tool mode uses 4 payloads.



## Two Runtime Modes

### Custom Payload Mode

If `Release\payload.exe` exists, the packaged result is:

```text
nativex64.exe | payload.exe | footer
```

Stage2 does the following:

1. Extracts the user payload from `Wpbbin.exe`.
2. Writes it to:

```text
C:\Windows\System32\GrabAccess.exe
```

3. Writes the autorun registry value:

```text
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run
  GrabAccessAutorun = C:\Windows\System32\GrabAccess.exe
```

4. Returns to the normal boot flow.



### Default Bypass Tool Mode

If `Release\payload.exe` does not exist, the packaged result is:

```text
nativex64.exe | Injector.exe | GrabAccessMsvpBypass.dll | GrabAccessExplorerHost.exe | GrabAccessFallback.exe | footer
```

Stage2 does the following:

1. Extracts:

```text
C:\Windows\System32\Injector.exe
C:\Windows\System32\GrabAccessMsvpBypass.dll
C:\Windows\System32\GrabAccessExplorerHost.exe
C:\Windows\System32\GrabAccessFallback.exe
```

2. Decides whether the current sign-in method can be patched.
3. Writes `GrabAccessRestore.bat`, `GrabAccessCleanup.bat`, `GrabAccessReason.txt`, and `GrabAccessMethod.txt`.
4. Sets the LogonUI IFEO Debugger value:

```text
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\LogonUI.exe
  Debugger = cmd.exe /c C:\Windows\System32\GrabAccessRestore.bat
```

5. Marks `Wpbbin.exe` for self-deletion.

When Windows later starts LogonUI, IFEO runs `GrabAccessRestore.bat` first.

If the sign-in method can be bypassed, `GrabAccessRestore.bat` injects `GrabAccessMsvpBypass.dll`, shows a 6-second prompt, and then continues to the real LogonUI. The user can then type any content into the password field to sign in.

If the sign-in method cannot be bypassed, `GrabAccessRestore.bat` starts `GrabAccessFallback.exe` first. This UI shows the fallback reason and provides three actions:

- Manage Files: starts `GrabAccessExplorerHost.exe`
- Command Prompt: starts the original `cmd.exe`
- Manage Accounts: starts `netplwiz.exe`

If `GrabAccessFallback.exe` is missing or fails to start, GrabAccess falls back to a maximized SYSTEM cmd.



## Which Sign-In Methods Can Be Bypassed

The current bypass patch is injected for the following sign-in methods:

- Local account + password
- Local account + PIN
- Local account + picture password

The injection command is:

```text
C:\Windows\System32\Injector.exe -n lsass.exe -i C:\Windows\System32\GrabAccessMsvpBypass.dll
```

After the user enters any input on the sign-in screen, the injected patch (the Stage3 DLL) hooks the sign-in authentication path and directly returns authentication success.



## Sign-In Methods That Cannot Be Bypassed

In the following cases, GrabAccess does not attempt to patch authentication. Instead, it opens `GrabAccessFallback.exe` during sign-in. It falls back to a SYSTEM cmd only when the fallback program is missing or fails to start:

- Microsoft Account
- Microsoft Entra ID / AzureAD account
- AD domain account
- Hello face
- Hello fingerprint
- Smart card, security key, or other unsupported Credential Provider
- `RunAsPPL` / protected LSASS is enabled
- The account source or Credential Provider cannot be identified reliably



## Dropped Files And Cleanup

Windows extracts Stage2 itself as:

```text
C:\Windows\System32\Wpbbin.exe
```

Before exiting, Stage2 marks it for self-deletion through `FILE_DELETE_ON_CLOSE`.

Custom payload mode keeps the following items persistently:

```text
C:\Windows\System32\GrabAccess.exe
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run\GrabAccessAutorun
```

Default bypass tool mode temporarily writes:

```text
C:\Windows\System32\Injector.exe
C:\Windows\System32\GrabAccessMsvpBypass.dll
C:\Windows\System32\GrabAccessExplorerHost.exe
C:\Windows\System32\GrabAccessFallback.exe
C:\Windows\System32\GrabAccessRestore.bat
C:\Windows\System32\GrabAccessCleanup.bat
C:\Windows\System32\GrabAccessReason.txt
C:\Windows\System32\GrabAccessMethod.txt
C:\Windows\System32\GA_AccountSource.txt
C:\Windows\System32\GA_AuthSeen.flag
C:\Windows\System32\ga_status.txt
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\LogonUI.exe\Debugger
```

After automatic patch succeeds, Stage3 writes `GA_AuthSeen.flag` and starts `GrabAccessCleanup.bat postauth`. The cleanup script waits for authentication to complete and for the desktop to appear, then cleans up LockApp / LogonUI leftovers, IFEO, temporary BAT files, Injector, the Stage3 DLL, Fallback, ExplorerHost, the flag file, the reason files, and the debug log.

On the unsupported fallback path, `GrabAccessFallback.exe` starts `GrabAccessCleanup.bat` when it exits, which removes IFEO and temporary files. `GrabAccessExplorerHost.exe` also attempts to delete itself when it exits.

`GrabAccessMsvpBypass.dll` cannot be deleted immediately because it is still held by a process, cleanup schedules it for deletion on the next reboot through `MoveFileEx(..., MOVEFILE_DELAY_UNTIL_REBOOT)`.



## Building

Prefer the one-click build script from the repository root:

```bat
make_all.bat --stage2-only
```

`BuildNativeAPP.bat` in this directory can also be used for building:

```bat
Stage2-NativeNT\BuildNativeAPP.bat
```
