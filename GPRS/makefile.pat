HDR=BSSG.h BSSGMessages.h ByteVector.h FEC.h GPRSExport.h GPRSInternal.h \
	GPRSTDMA.h MAC.h MsgBase.h GPRSRLC.h RLCEngine.h RLCHdr.h RLCMessages.h RList.h \
	ScalarTypes.h TBF.h MSInfo.h
SRC1= ByteVector.cpp \
	MSInfo.cpp TBF.cpp FEC.cpp RLCEngine.cpp RLC.cpp MAC.cpp \
	BSSG.cpp BSSGMessages.cpp GPRSCLI.cpp \
	RLCMessages.cpp RLCEngine.cpp MsgBase.cpp
# Compile the most recently modified ones first.
SRC=$(shell ls -t $(SRC1))
#CSRC= iputils.c

INCLUDE= -I. -I.. -I../SGSNGGSN -I../CommonLibs -I../Control -I../GPRS -I../GSM -I../SIP -I../SMS -I../TRXManager -I../Globals -I../CLI -I../HLR -I../SR -I../sqlite3

ODIR=.libs

GPRSOBJ= $(SRC:%.cpp=$(ODIR)/%.o)
COBJ= $(CSRC:%.c=$(ODIR)/%.o)
OBJ= $(COBJ) $(GPRSOBJ)


default: a
#default: Makefile.am a

# 'all' is the target made by ../Makefile
all:
	make -f Makefile

more:
	(clear && make lib && cd ../apps && make) 2>&1 | more

a: .ALWAYS
	#make lib && (cd ..; make)
	make -f Makefile && (cd ..; make)


g: $(GGSNOBJ)
g2: miniggsn.o iputils.o

# The at-sign makes it not echo the program, so you can do: make sql > gprs.sql
gprs.sql: .ALWAYS
	@awk '/BEGINCONFIG/,/ENDCONFIG/ { \
		if (/BEGINCONFIG/||/ENDCONFIG/) next; \
		sub("^[^/]*//",""); \
		commas=$$0; gsub("[^,]*","",commas); \
		if (length(commas) < 4) print "syntax error in",FILENAME,":",$$0 >"/dev/tty"; \
		print "INSERT INTO \"CONFIG\" VALUES(" $$0 ");" \
		}' *.cpp > gprs.sql

test1: test1.cpp Makefile libGPRS.a
	g++ $(INCLUDE) -o test1 test1.cpp libGPRS.a ../CommonLibs/.libs/libcommon.a

crc: crc24.c
	gcc -o crc crc24.c

test2: test1.cpp Makefile libGPRS.a
	g++ $(INCLUDE) -o test1 test1.cpp libGPRS.a ../CommonLibs/.libs/libcommon.a ../GSM/.libs/libGSM.a

testbv: ByteVector.cpp ByteVector.h makefile
	g++ $(INCLUDE) -g -o testbv -DTEST=1 ByteVector.cpp ../CommonLibs/.libs/libcommon.a

lib: $(OBJ)
	ar cru $(ODIR)/libGPRS.a $(OBJ)
	touch libGPRS.la

#.cpp.o:
$(ODIR)/%.o: %.cpp
	-mkdir $(ODIR) 2>/dev/null
	g++ -O0 -DHAVE_CONFIG_H $(INCLUDE)  -Wall -g -c -o $(ODIR)/$*.o $*.cpp

$(ODIR)/%.o: %.c
	-mkdir $(ODIR) 2>/dev/null
	g++ -O0 -DHAVE_CONFIG_H $(INCLUDE) -Wall -g -c -o $(ODIR)/$*.o $*.c




# g++ -DHAVE_CONFIG_H -I. -I. -I..  -I../CommonLibs -I../Control -I../GPRS -I../GSM -I../SIP -I../SMS -I../TRXManager -I../Globals -I../CLI -I../HLR -I../SR -I../sqlite3  -Wall -O3 -g -O2 -MT RadioResource.lo -MD -MP -MF ".deps/RadioResource.Tpo" -c -o RadioResource.lo RadioResource.cpp; \
		        then mv -f ".deps/RadioResource.Tpo" ".deps/RadioResource.Plo"; else rm -f ".deps/RadioResource.Tpo"; exit 1; fi

$(OBJ):$(HDR)
$(ODIR)/miniggsn.o $(ODIR)/iputils.o: miniggsn.h Ggsn.h

svnadd:
	svn add $(HDR) $(SRC)

clean:
	/bin/rm $(ODIR)/*

commit:
	svn commit $(HDR) $(SRC)

pinghttp: pinghttp.c
	gcc -DSTANDALONE=1 -o pinghttp pinghttp.c

# Need a short name for DOS file system.
SMALLFILES= GPRS/*.[hc]* GSM/*.[hc]* CLI/*.[hc]* \
	CommonLibs/*.[hc]* Control/*.[hc]* TRXManager/*.[hc]*
small:
	cd .. && tar -czvf GPRS_backup_`date +%m-%d`.tgz $(SMALLFILES) \
		--no-recursion
backup:
	cd .. && tar -czvf GPRS_full_`date +%m-%d`.tgz */* \
		--exclude .svn --exclude .deps --exclude .libs --exclude 'sqlite*' \
		--exclude '*o' --exclude '*.asn' --exclude '*cache*' --exclude 'Trans*' \
		--exclude OpenBTS --exclude *Test --exclude bk*

ctags tags: .ALWAYS
	cd ..; sh PAT.ctags

.ALWAYS:


# Evidently the makefile autogenerator doesnt work, because David complains
# every time he tries to make this directory.  So lets just write out the
# # automake makefile generator file to try to make him happy.
# This is pretty dumb, making an auto-make makefile from a makefile.
# Rebuild it whenever this makefile changes:
Makefile.am: makefile
	@: Start with the copyright:
	@sed -n '1,/^$$/p' < ../Makefile.am > Makefile.am
	@awk >> Makefile.am '\
		BEGIN { \
		print "include $$(top_srcdir)/Makefile.common\n"; \
		print "AM_CPPFLAGS = $$(STD_DEFINES_AND_INCLUDES)\n"; \
		print "#AM_CXXFLAGS = -O2 -g\n"; \
		print "noinst_LTLIBRARIES = libGPRS.la\n"; \
		src="$(SRC)"; gsub(" +"," \\\n\t",src); \
		hdr="$(HDR)"; gsub(" +"," \\\n\t",hdr); \
		print "\nlibGPRS_la_SOURCES = \\"; print "\t" src; \
		print "\nnoinst_HEADERS = \\"; print "\t" hdr; \
		}'


#==============================================
# These are the lines that modified the existing file, but I decided to just overwrite:
# /libGPRS_la_SOURCES/,/^$$/ { next }
# /noinst_HEADERS/,/^$$/ { next }
#{print}
