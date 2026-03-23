@echo off
setlocal

cd /d "%~dp0"

if exist ".venv\Scripts\python.exe" (
    set "PYTHON=.venv\Scripts\python.exe"
    goto run
)

where py >nul 2>nul
if %errorlevel%==0 (
    py -3 -c "import serial" >nul 2>nul
    if not %errorlevel%==0 goto missing_serial_py
    py -3 uart_acquisition_ui.py
    set "STATUS=%errorlevel%"
    goto end
)

where python >nul 2>nul
if not %errorlevel%==0 (
    echo Python introuvable.
    echo Installe Python 3 puis relance ce fichier.
    pause
    exit /b 1
)

set "PYTHON=python"

goto run

:run
%PYTHON% -c "import serial" >nul 2>nul
if not %errorlevel%==0 goto missing_serial

%PYTHON% uart_acquisition_ui.py
set "STATUS=%errorlevel%"
goto end

:missing_serial_py
echo Le module pyserial est manquant.
echo Commande conseillee: py -3 -m pip install pyserial
pause
exit /b 1

:missing_serial
echo Le module pyserial est manquant.
echo Commande conseillee: %PYTHON% -m pip install pyserial
pause
exit /b 1

:end
if not "%STATUS%"=="0" (
    echo L'application s'est arretee avec le code: %STATUS%
    pause
)

exit /b %STATUS%
