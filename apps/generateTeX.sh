#!/bin/sh

sqlite3 -separator "" $1 "select '\item ',KEYSTRING,' -- ',COMMENTS from CONFIG order by KEYSTRING;"
