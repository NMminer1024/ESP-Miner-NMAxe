@echo off
start "benchmark-QAxe++"  benchmark.exe -vr 1100,1300 -vs 50 -fr 540,750  -fs 50 -si 5 -bt 800 -ip 192.168.124.22
start "benchmark-QAxe++Rev6.1" benchmark.exe -vr 1150,1450 -vs 50 -fr 540,1000 -fs 50 -si 5 -bt 800 -ip 192.168.124.241
