#!/bin/bash
ping 10.0.0.42
cat ping.txt | cut -f7 -d' ' | cut -f2 -d=
