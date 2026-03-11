#!/bin/sh
gcc -Wall -g server.c db_ops.c -o nettransit -lcjson -lsqlite3
./nettransit