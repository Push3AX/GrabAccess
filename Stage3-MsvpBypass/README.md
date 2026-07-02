# GrabAccess Stage3-MsvpBypass

`Stage3-MsvpBypass` 是用于绕过登录认证的DLL。它会被 `Stage3-Injector` 注入到 `lsass.exe`，然后 hook 本地账号认证路径中的 `NtlmShared!MsvpPasswordValidate`，使得无论输入任何密码，都返回认证通过。

其次它还负责调用GrabAccessCleanup.bat进行最后的清理工作。

可以使用仓库根目录的一键构建脚本编译此DLL：

```bat
make_all.bat --stage3-only
```



---

# GrabAccess Stage3-MsvpBypass

`Stage3-MsvpBypass` is the DLL used to bypass sign-in authentication. It is injected into `lsass.exe` by `Stage3-Injector`, then hooks `NtlmShared!MsvpPasswordValidate` in the local-account authentication path so authentication succeeds regardless of the password entered.

It is also responsible for calling `GrabAccessCleanup.bat` to perform the final cleanup work.

This DLL can be built with the one-click build script in the repository root:

```bat
make_all.bat --stage3-only
```
