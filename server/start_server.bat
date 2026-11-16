@echo off
chcp 65001 >nul
title MyrLegit Server Database & Auth
echo Запуск сервера базы данных и авторизации MyrLegit...
powershell -ExecutionPolicy Bypass -File "%~dp0server.ps1"
pause

