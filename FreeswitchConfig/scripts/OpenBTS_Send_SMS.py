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

from freeswitch import *
import sys
import re
import random
import messaging.sms.submit
from datetime import datetime, timedelta

#RP-Message Type = 00
#RP-Message Reference = GG -fill out later
#RP-Originator Address = 00
#RP-Destination Address = 9999
RP_GENERIC_HEADER = '00GG0003919999'
#TP-Message Type = 11 - SMS_SUBMIT
#TP-Message Reference = GG - fill out later
TP_GENERIC_HEADER = '11GG'

#MAX GSM time is 63 weeks
MAX_GSM_TIME = 63 * 7

def gen_header(reference, header):
    return re.sub('GG', reference, header)

#gotta preserve 0s...
def gen_hex(i):
    tmp = hex(i)[2:]
    if (len(tmp) == 1):
        return "0" + tmp
    else:
        return tmp

def gen_tpdu(to, text):
    tmp = (messaging.sms.submit.SmsSubmit(str(to), text))
    tmp._validity = timedelta(MAX_GSM_TIME)
    #stripping the nonsense headers, will probably fix later
    return tmp.to_pdu()[0].pdu[6:].lower() 

def gen_body(to, text):
    reference = str(hex(random.randint(17,255))[2:]) #random reference?
    rp_header = gen_header(reference, RP_GENERIC_HEADER)
    tp_header = gen_header(reference, TP_GENERIC_HEADER)
    tp_user_data = gen_tpdu(to, text)
    tp_len = (len(tp_header) + len(tp_user_data))/2 #octets, not bytes
    return rp_header + gen_hex(tp_len) + tp_header + tp_user_data

#forward the message to smqueue for store-and-forwarding
def send_smqueue_message(to, fromm, text):
    if not(getGlobalVariable("domain")):
        consoleLog('err', "Global var 'domain' not set\n")
        exit(1)
    if not(getGlobalVariable("smqueue_profile")):
        consoleLog('err',"Global var 'smqueue_profile' not set\n")
        exit(1)
    if not(getGlobalVariable("smqueue_host")):
        consoleLog('err',"Global var 'smqueue_host' not set\n")
        exit(1)
    if not(getGlobalVariable("smqueue_port")):
        consoleLog('err',"Global var 'smqueue_port' not set\n")
        exit(1)
    event = Event("CUSTOM", "SMS::SEND_MESSAGE")
    event.addHeader("proto", "sip");
    event.addHeader("dest_proto", "sip");
    event.addHeader("from", fromm)
    event.addHeader("from_full", "sip:" + fromm + "@" + getGlobalVariable("domain"))
    event.addHeader("to", getGlobalVariable("smqueue_profile") + "/sip:smsc@" + getGlobalVariable("smqueue_host") + ":" + getGlobalVariable("smqueue_port"))
    event.addHeader("subject", "SIMPLE_MESSAGE")
    event.addHeader("type", "application/vnd.3gpp.sms");
    event.addHeader("hint", "the hint");
    event.addHeader("replying", "false");
    event.addBody(gen_body(to, text));

    event.fire()

def chat(message, args):
    args = args.split('|')
    if (len(args) < 3):
        consoleLog('err', 'Missing Args\n')
        exit(1)
    to = args[0]
    fromm = args[1]
    text = args[2]
    if ((not to or to == '') or
        (not fromm or fromm == '')):
        consoleLog('err', 'Malformed Args\n')
        exit(1)
    send_smqueue_message(to, fromm, text)

def fsapi(session, stream, env, args):
    #chat doesn't use message anyhow
    chat(None, args)
