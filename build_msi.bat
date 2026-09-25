@echo off
setlocal
cd /d "%~dp0"

echo ===================================================
echo   Creation de l'installeur MSI pour Gastinel Write
echo ===================================================

if not exist "build\gastinel_write.exe" (
    echo [ERREUR] Le binaire build\gastinel_write.exe n'existe pas.
    echo Veuillez d'abord compiler le projet.
    pause
    exit /b 1
)

if not exist "build\wix_bin\candle.exe" (
    echo [ERREUR] WiX Toolset est introuvable dans build\wix_bin.
    pause
    exit /b 1
)

echo 1. Compilation WiX (candle)...
"build\wix_bin\candle.exe" -nologo -arch x64 -ext WixUtilExtension -dSourceDir="%CD%" -out "build\gastinel_write.wixobj" "installer\gastinel_write.wxs"
if %ERRORLEVEL% neq 0 (
    echo [ERREUR] Echec lors de la compilation candle.
    pause
    exit /b %ERRORLEVEL%
)

echo 2. Liaison MSI (light)...
"build\wix_bin\light.exe" -nologo -ext WixUIExtension -ext WixUtilExtension -sice:ICE60 -out "build\GastinelWrite-Setup.msi" "build\gastinel_write.wixobj"
if %ERRORLEVEL% neq 0 (
    echo [ERREUR] Echec lors de la liaison light.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ===================================================
echo   Installeur genere avec succes :
echo   %CD%\build\GastinelWrite-Setup.msi
echo ===================================================
endlocal
