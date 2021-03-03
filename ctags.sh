#DIRS="AsteriskConfig tools"
DIRS="CLI CommonLibs Control GPRS GSM GSMShare Globals Peering
	SGSNGGSN SIP SMS SR TRXManager TransceiverRAD1 apps
# sqlite3 doc pat

files=""
for dir in $DIRS;do
    files="$files $dir/*.h $dir/*.c*"
done

eval echo $files
# Ignore PACKED keyword
eval ctags -I PACKED --extra=+fq $files

