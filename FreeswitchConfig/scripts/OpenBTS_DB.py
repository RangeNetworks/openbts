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
from freeswitch import *

def get_target(cur, destination):
    cur.execute('select name from sip_buddies where callerid=?', (destination,))
    res = cur.fetchone()
    if (res):
        return res[0]
    else:
        return res

def get_caller_id(cur, caller):
    cur.execute('select callerid from sip_buddies where name=?', (caller,))
    res = cur.fetchone()
    if (res):
        return res[0]
    else:
        return res 

def usage(stream, code):
    stream.write("ERROR: %d" % code)
    exit(1)

def fsapi(session, stream, env, args):
    db_loc = None
    caller = None
    destination = None


    opts, args = getopt.getopt(args.split(" "), "d:c:t:", ["db=", "caller=", "target="])
    
    for o,a in opts:
        if o in ("-d", "--db="):
            db_loc = a
        elif o in ("-c", "--caller="):
            caller = a
        elif o in ("-t", "--target="):
            destination = a
        else:
            usage(stream,0)
            
    if (not db_loc):
        usage(stream,1)
    if (caller and destination):
        usage(stream,2)

    conn = sqlite3.connect(db_loc)
    cur = conn.cursor()
    if (caller):
        stream.write(str(get_caller_id(cur, caller)))
    else:
        stream.write(str(get_target(cur, destination)))
    conn.close()

#not using this now
def handler(session,args):
    db_loc = session.getVariable("db_loc")
    caller = session.getVariable("username")
    destination = session.getVariable("destination_number")
    if (db_loc and caller and destination):
        res = get_vars(db_loc, caller, destination)
    

