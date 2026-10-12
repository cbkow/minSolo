@echo off
rem build-setup.cmd — stage minSolo.exe and run Inno Setup 6.
rem   Needs: a Release build in build\ (cmake -G Ninja ...), Inno Setup 6.
rem   Output: dist\minSolo-<version>-Setup-x64.exe (three levels up: the repo's dist\)
setlocal
set "BUILD=%~dp0..\build"
set "STAGE=%~dp0..\build\stage"
for /f "tokens=4 delims=() " %%v in ('findstr /c:"project(minSolo VERSION" "%~dp0..\CMakeLists.txt"') do set "VERSION=%%v"
if "%VERSION%"=="" ( echo ERROR: could not read the version from CMakeLists.txt & exit /b 1 )
if not exist "%BUILD%\minSolo.exe" ( echo ERROR: build\minSolo.exe missing & exit /b 1 )
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
copy /y "%BUILD%\minSolo.exe" "%STAGE%\" >nul
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /Q /DMyAppVersion=%VERSION% "%~dp0minSolo.iss" || ( echo ERROR: Inno failed & exit /b 1 )
echo SETUP OK %VERSION%
