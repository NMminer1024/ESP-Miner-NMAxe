@echo off
start "benchmark-QAxe++"  benchmark.exe -vr 1100,1300 -vs 25 -fr 440,750  -fs 25 -si 5 -bt 800 -ip 192.168.124.22
start "benchmark-QAxe++Rev6.1" benchmark.exe -vr 1150,1450 -vs 25 -fr 540,1000 -fs 25 -si 5 -bt 800 -ip 192.168.124.241
