@echo off
rem Builds Mario64GTA.asi (ScriptHookV script) with MSVC into build\. Run fetch_deps.sh first.
rem sm64.dll is not linked: the script loads it at run time from <GTA V>\Mario64GTA\.
rem Uses the newest Visual Studio with the C++ x64 tools; set VCVARS to another vcvars64.bat to pick one.
setlocal
set HERE=%~dp0
if not defined VCVARS for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%HERE%third_party\shv\ScriptHookV.lib" (echo third_party is missing: run fetch_deps.sh first& exit /b 1)
if not exist "%HERE%third_party\libsm64\src\libsm64.h" (echo third_party\libsm64 is missing: run fetch_deps.sh first& exit /b 1)
call "%VCVARS%" >nul || exit /b 1
if not exist "%HERE%build" mkdir "%HERE%build"
cl /nologo /LD /O2 /EHsc /std:c++17 /MT /W3 /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /I "%HERE%third_party\shv" /I "%HERE%third_party\libsm64\src" /I "%HERE%src" ^
  "%HERE%src\script.cpp" "%HERE%src\core\camera.cpp" "%HERE%src\core\collision.cpp" ^
  "%HERE%src\core\combat.cpp" "%HERE%src\core\mario.cpp" ^
  /Fo"%HERE%build\\" /Fe"%HERE%build\Mario64GTA.asi" ^
  /link "%HERE%third_party\shv\ScriptHookV.lib" user32.lib
