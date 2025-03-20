@echo off
echo Starting server and clients for testing...

REM Start the server
start cmd /k "ServerApplication.exe"

REM Wait for server to initialize
timeout /t 2 /nobreak

REM Start 10 clients
FOR /L %%i IN (1,1,10) DO (
    start cmd /k "ClientApplication.exe 127.0.0.1 8080"
    timeout /t 1 /nobreak
)

echo All processes started. Let them run for 10-20 minutes, then press ESC in each window to terminate.