@echo off
chcp 65001 >nul
title MyrLegit Server Database & Auth

netsh http show urlacl url=http://+:8000/ | findstr /i "http://+:8000/" >nul
if %errorlevel% neq 0 (
    echo [Настройка] Регистрация сетевого порта 8000 для подключений друзей...
    powershell -Command "Start-Process netsh -ArgumentList 'http add urlacl url=http://+:8000/ sddl=D:(A;;GX;;;WD)' -Verb RunAs -Wait" 2>nul
)

echo Запуск сервера базы данных и авторизации MyrLegit...
powershell -ExecutionPolicy Bypass -File "%~dp0server\server.ps1"
pause

