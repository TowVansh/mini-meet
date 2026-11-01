@echo off
title Mini-Meet client
mode con cols=125 lines=24
cd /d E:\Projects\MiniMeet\bin
echo ^> mm.exe --server 127.0.0.1:9703 --room demo --name PC1 --test --no-mirror --duration 30
E:\Projects\MiniMeet\bin\mm.exe --server 127.0.0.1:9703 --room demo --name PC1 --test --no-mirror --duration 30
pause
