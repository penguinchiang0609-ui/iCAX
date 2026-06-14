@echo off
setlocal enabledelayedexpansion

echo ==========================================
echo FlatBuffers Code Generator
echo ==========================================

REM 当前目录（flatc.exe 和本 bat 所在目录）
set ROOT=%~dp0

REM 路径
set FBS_DIR=%ROOT%fbs
set CPP_GEN=%ROOT%include
set CSHARP_GEN=%ROOT%net

REM 如果目录不存在则创建
if not exist "%CPP_GEN%" mkdir "%CPP_GEN%"
if not exist "%CSHARP_GEN%" mkdir "%CSHARP_GEN%"

echo Using flatc at: %ROOT%flatc.exe
echo FBS directory:  %FBS_DIR%
echo C++ output:     %CPP_GEN%
echo C# output:      %CSHARP_GEN%

echo.

REM 遍历所有 fbs 文件
for %%f in ("%FBS_DIR%\*.fbs") do (
    echo Generating for %%~nxf...

    REM 生成 C++
    "%ROOT%flatc.exe" --cpp --gen-object-api -o "%CPP_GEN%" "%%f"

    REM 生成 C#
    "%ROOT%flatc.exe" --csharp -o "%CSHARP_GEN%" "%%f"
)

echo.
echo ==========================================
echo Done. All fbs files generated successfully.
echo ==========================================
pause
