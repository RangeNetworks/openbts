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
import getopt
import re
import random
from freeswitch import *

def err(msg):
    consoleLog("err", str(msg))
    exit(1)

#this is probably silly
def gen_random_hex(length):
    res = ''
    for x in range(0,length-1):
        res += "%x" % (random.randint(0,15))
    return res

def create_user(db_loc, caller, target, ip, port):

    try:
        conn = sqlite3.connect(db_loc)
        cur = conn.cursor()
    except:
        err("Bad DB\n")

    #do they already have a number?
    cur.execute('SELECT callerid from sip_buddies where name=?', (caller,))
    res = cur.fetchone()
    if (res):
        return ("You already have a number: %s" % res[0])
    #is that number taken?
    cur.execute('SELECT * from sip_buddies WHERE callerid=?', (target,))
    if (cur.fetchone()):
        return ("Number %s already taken!" % target)
    #ok, give them the number
    cur.execute("INSERT INTO sip_buddies (name, username, type, context, host, callerid, canreinvite, allow, dtmfmode, ipaddr, port) values (?,?,?,?,?,?,?,?,?,?,?)", (caller, caller, "friend", "phones", "dynamic", target, "no", "gsm", "info", ip, port))
    conn.commit()
    cur.execute("INSERT INTO dialdata_table (exten, dial) values (?, ?)", (target, caller))
    conn.commit()
    conn.close()
    return ("Your new number is %s" % target)
    
def chat(message, args):
    db_loc = getGlobalVariable("openbts_db_loc")
    caller = message.getHeader("from_user")
    target = message.getHeader("openbts_text")
    ip = message.getHeader("from_host")
    port = message.getHeader("from_sip_port")

    if not (db_loc):
        err("openbts_db_loc not defined\n")
    elif not (caller):
        err("from_user not defined\n")
    elif not (target):
        err("openbts_text not defined\n")
    elif not (port):
        err("from_sip_port not defined\n")

    res = str(create_user(db_loc, caller, target, ip, port))
    message.chat_execute('set', '_openbts_ret=%s' % res)
    consoleLog('info', "Sent '%s' to %s\n" % (res, caller))

def fsapi(session, stream, env, args):
    args = args.split('|')
    if (len(args) < 4):
        err('Missing Args\n')
    caller = args[0]
    target = args[1]
    ip = args[2]
    port = args[3]

    db_loc = None
    if (len(args) == 5):
        db_loc = args[4]

    #if they don't all exist
    if (not db_loc or db_loc == ''):
        db_loc = getGlobalVariable("openbts_db_loc")

    if (not db_loc):
        err("Missing DB. Is openbts_db_loc defined?\n")

    if not (caller and target and port):
        err("Malformed Args \n")

    if (caller == '' or
        target == '' or
        port == ''):
        err("Malformed Args \n")

    stream.write(str(create_user(db_loc, caller, target, ip, port)))
