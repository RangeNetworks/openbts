#!/usr/bin/python


import sys


if len(sys.argv)<2:
	print sys.argv[0]," configFile"
	sys.exit()

filename = sys.argv[1]


# This needs to match what is in Configuration.cpp.
header = " \
CREATE TABLE IF NOT EXISTS CONFIG ( \
KEYSTRING TEXT UNIQUE NOT NULL, \
VALUESTRING TEXT, \
STATIC INTEGER DEFAULT 0, \
OPTIONAL INTEGER DEFAULT 0, \
COMMENTS TEXT DEFAULT '' \
);"


inFile = open(filename,"r")

knownKeys = []


def processDirective(dirLine):
	(directive,key) = dirLine.split(' ')
	if directive=="static":
		if key not in knownKeys:
			print "non-existant key",key,"cannot be static"
			sys.exit()
		print "UPDATE CONFIG SET STATIC=1 WHERE KEYSTRING==\""+key+"\";"
		return
	if directive=="optional":
		if key not in knownKeys:
			print "INSERT INTO CONFIG (KEYSTRING,OPTIONAL) VALUES (\""+key+"\",1);"
			return
		print "UPDATE CONFIG SET OPTIONAL=1 WHERE KEYSTRING==\""+key+"\";"
		return
	print "unknown directive",directive
	sys.exit()


print header

for line in inFile:
	line = line[:-1].lstrip()
	if len(line)==0:
		continue
	# process comments
	if line[0]=='#':
		print "--", line
		continue
	# process directive
	if line[0]=='$':
		processDirective(line[1:])
		continue
	# key-value pairs
	keyval = line.split(' ',1)
	key = keyval[0]
	if key in knownKeys:
		print "key",key,"not unique"
		sys.exit()
	if len(keyval)==1:
		print "INSERT INTO CONFIG (KEYSTRING) VALUES (\""+key+"\");"
	else:
		value = keyval[1]
		print "INSERT INTO CONFIG (KEYSTRING,VALUESTRING) VALUES (\""+key+"\",\""+value+"\");"
	knownKeys.append(key)
inFile.close()





