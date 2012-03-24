#Copyright 2011 Kurtis Heimerl <kheimerl@cs.berkeley.edu>. All rights reserved.
#
#Redistribution and use in source and binary forms, with or without modification, are
#permitted provided that the following conditions are met:
#
#   1. Redistributions of source code must retain the above copyright notice, this list of
#      conditions and the following disclaimer.
#
#   2. Redistributions in binary form must reproduce the above copyright notice, this list
#      of conditions and the following disclaimer in the documentation and/or other materials
#      provided with the distribution.
#
#THIS SOFTWARE IS PROVIDED BY Kurtis Heimerl ''AS IS'' AND ANY EXPRESS OR IMPLIED
#WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
#FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Kurtis Heimerl OR
#CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
#ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
#ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#The views and conclusions contained in the software and documentation are those of the
#authors and should not be interpreted as representing official policies, either expressed
#or implied, of Kurtis Heimerl.

import csv
import sqlite3
import sys
from freeswitch import *

def execute_cmd(db_loc, cmd):
    conn = sqlite3.connect(db_loc)
    cur = conn.cursor()
    cur.execute(cmd)
    res = cur.fetchone()
    conn.close()
    return res

def err(msg):
    consoleLog(msg)
    exit(1)

def parse_and_op(args):
    sys.stderr.write(args + "\n")
    args = args.split('|')
    cmd = args[0]
    db_loc = getGlobalVariable('openbts_db_loc')
    if (len(args) > 1):
        db_loc = args[1]

    if not(db_loc):
       err('Missing DB. Is openbts_db_loc defined?\n') 
    
    try:
        res = execute_cmd(db_loc, cmd)
        return str(res[0])
    except Exception as err:
        consoleLog('err', str(err) + "\n")
        exit(1)

def chat(message, args):
    res = parse_and_op(args)
    consoleLog('info', "Returned: " + res + "\n")
    message.chat_execute('set', '_openbts_ret=%s' % res)

def fsapi(session, stream, env, args):
    res = parse_and_op(args)
    consoleLog('info', "Returned: " + res)
    if (res):
        stream.write(str(res))

