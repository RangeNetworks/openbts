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
import getopt
import sys
import messaging.utils

#util functions
def strip_fs(s):
    if (s[-1] in ['f', 'F']):
        return s[:-1]
    else:
        return s

#byte order is backwards...
def octet_to_number(o):
    i = 0
    res = ''
    while (i < len(o)):
        res += o[i+1]
        res += o[i]
        i += 2
    return res

def n_bytes(h,n):
    (hex_str, index) = h
    return (hex_str[index:index+n], (hex_str, index+n))

#parse functions
def get_rp_message_type(h):
    return n_bytes(h,2)

def get_rp_message_reference(h):
    return n_bytes(h,2)

def get_rp_originator_address(h):
    return n_bytes(h,2)

def get_rp_destination_address(h):
    (num_octets,h) = n_bytes(h,2)
    num_octets = int(num_octets,16)
    (rp_dest_address_type,h) = n_bytes(h,2)
    (rp_dest_address,h) = n_bytes(h,(num_octets-1)*2) #minus for address type, *2 as octets
    return (rp_dest_address_type, strip_fs(octet_to_number(rp_dest_address)), h)

def get_rp_user_data(h):
    (num_octets,h) = n_bytes(h,2)
    num_octets = int(num_octets,16)*2
    if ((len(h[0]) - h[1]) != num_octets):
        raise Exception("MALFORMED MESSAGE: Bad RP-User-Data length")
    return h
    
def get_tp_message_type(h):
    return n_bytes(h,2)

def get_tp_message_reference(h):
    return n_bytes(h,2)

def get_tp_destination_address(h):
    (num_bytes,h) = n_bytes(h,2) #not octets!
    num_bytes = int(num_bytes,16)
    if (num_bytes % 2 == 1):
        num_bytes += 1 #has to be even number, they will have inserted extraneous Fs
    (tp_destination_address_type, h) = n_bytes(h,2)
    (tp_destination_address,h) = n_bytes(h,num_bytes)
    return (tp_destination_address_type, strip_fs(octet_to_number(tp_destination_address)), h)

def get_tp_protocol_identifier(h):
    return n_bytes(h,2)

def get_tp_data_coding_scheme(h):
    return n_bytes(h,2)

def get_tp_validity_period(h):
    return n_bytes(h,2)

#hax for now, just read rest of msg
def get_tp_user_data(h):
    (num_septets,h) =  n_bytes(h,2)
    #num_bits = int(num_septets,16) * 7
    #sys.stderr.write(str(num_bits))
    #return (n_bytes(h,(num_bits+3)/4)) #rounding up
    return (n_bytes(h, len(h[0]) - h[1]))

def parse(rp_message):
    rp_message = (rp_message,0)
    (rp_message_type, rp_message) = get_rp_message_type(rp_message)
    (rp_message_reference, rp_message) = get_rp_message_reference(rp_message)
    (rp_originator_address, rp_message) = get_rp_originator_address(rp_message)
    (rp_dest_address_type, rp_dest_address, rp_message) = get_rp_destination_address(rp_message)
    rp_user_data = get_rp_user_data(rp_message)

    #rp_message finished
    (tp_message_type, rp_user_data) = get_tp_message_type(rp_user_data)
    (tp_message_reference, rp_user_data) = get_tp_message_reference(rp_user_data)
    (tp_dest_address_type, tp_dest_address, rp_user_data) = get_tp_destination_address(rp_user_data)
    (tp_protocol_id, rp_user_data) = get_tp_protocol_identifier(rp_user_data)
    (tp_data_coding_scheme, rp_user_data) = get_tp_data_coding_scheme(rp_user_data)
    #check to see if validity period field is there
    if (int(tp_message_type, 16) & 0x10 == 0):
        tp_validity_period = None
    else:
        (tp_validity_period, rp_user_data) = get_tp_validity_period(rp_user_data)
    (tp_user_data, rp_user_data) = get_tp_user_data(rp_user_data)

    sys.stderr.write(tp_user_data)

    return {"openbts_rp_message_type" : rp_message_type,
            "openbts_rp_message_reference" : rp_message_reference,
            "openbts_rp_originator_address" : rp_originator_address,
            "openbts_rp_dest_address_type" : rp_dest_address_type,
            "openbts_rp_dest_address" : rp_dest_address,
            "openbts_tp_message_type" : tp_message_type,
            "openbts_tp_message_reference" : tp_message_reference,
            "openbts_tp_dest_address_type" : tp_dest_address_type,
            "openbts_tp_dest_address" : tp_dest_address,
            "openbts_tp_protocol_id" : tp_protocol_id,
            "openbts_tp_data_coding_scheme" : tp_data_coding_scheme,
            "openbts_tp_validity_period" : tp_validity_period,
            "openbts_tp_user_data" : tp_user_data,
            "openbts_text" : messaging.utils.unpack_msg(tp_user_data).encode('UTF8').rstrip('\0')
            }

def chat(message, args):
    try:
        content = parse(message.getBody())
        for key in content.keys():
            message.chat_execute('set', '%s=%s' % (key, content[key]))
    except Exception as err:
        consoleLog('err', str(err))
        sys.stderr.write(str(err))
        exit(1)

def fsapi(session, stream, env, args):
    consoleLog('err', 'Cannot call Parse_SMS from the FS API\n')
    exit(1)

if __name__ == '__main__':
    if (len(sys.argv) < 2):
        print ("GIVE IT A HEX STRING!")
        exit(1)
    res = parse(sys.argv[1])
    print (res)

else:
    from freeswitch import *

