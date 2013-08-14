#!/bin/sh

sqlite3 /etc/OpenBTS/OpenBTS.db ".dump CONFIG" > OpenBTS.export.sql
