Welcome to the OpenBTS  source code reloaded for 2023 supporting new UHD drivers and Ubuntu 22.04 LTS to be compiled against C++11 and C++17.

# What is this project?

This projects provides a GSM+GPRS Radio Access Network Node with Software-Defined Radio.

# Supported hardware

* USRPs:
	* USRP v1 (with 52 MHz clock -> but can be patched for default 64 MHz) 
	* USRP2
	* USRP B200/B210 and B205mini-*
	* USRP N210
	* USRP X3*0
	* USRP N210
* UmTRX
* LimeSDR with OsmoTRX transceiver by now
* ANTSDR E200: Warning! Super experimental as we don't own the hardware yet (send your captures centered on the used ARFCN for debug)

# Quick usage

## Setup

Clone the repository and use the pre-installation script `preinstall.sh` to clone all other projects, submodules and install dependencies:

```
$ git clone https://github.com/PentHertz/OpenBTS.git
$ cd OpenBTS
$ # Optionally, checkout 5.1.0 branch which is the stable one with `git checkout 5.1.0`
$ ./preinstall.sh # note that for now libcoredumper will show some failures but we quickly bypass them forcing the compilation
```

Once it is finished, you can proceed with the installation of OpenBTS as follows:

```
$ ./autogen.sh
$ ./configure --with-uhd # use different options for other drivers
$ make -j$(nproc)
$ sudo make install
$ sudo ldconfig
```

And then we can launch everything!

## Running everything

Preferably run the probe UHD tool to load the firmware and FPGA into the USRP first:

```
$ uhd_usrp_probe 
[INFO] [UHD] linux; GNU C++ version 11.2.0; Boost_107400; UHD_4.1.0.5-3
[INFO] [B200] Loading firmware image: /usr/share/uhd/images/usrp_b200_fw.hex...
[INFO] [B200] Detected Device: B210
[INFO] [B200] Loading FPGA image: /usr/share/uhd/images/usrp_b210_fpga.bin...
[...]
```

You can use `screen`, `tmux` or just laucnh everything except `OpenBTS` in background:

```
$ sudo smqueue &
$ sudo sipauthserve &
$ sudo /OpenBTS/OpenBTS
```

And voilà!

# Docker container

A Docker images has been also generated for the backup and is ready to use with all installed tools: https://hub.docker.com/r/penthertz/openbts

# Fuzzing with OpenBTS

The Testcall feature has been reintroduced and includes also a SMS Fuzzing features thanks to @Djimmer work, and is under test with this new version of OpenBTS.

You can already test it with the `fuzzing-dev` branch:

```
git checkout fuzzing-dev
```

Do not hesitate to send issue or pull requests in order to stabilize it.


# Old README

For free support, please subscribe to openbts-discuss@lists.sourceforge.net.
See http://sourceforge.net/mailarchive/forum.php?forum_name=openbts-discuss
and https://lists.sourceforge.net/lists/listinfo/openbts-discuss for details.

A5/3 support requires installation of liba53.  This can be installed from:
git@github.com:RangeNetworks/liba53.git

Starting with release 4, OpenBTS requires zeromq (zmq).  This can be installed by running:
$ sudo ./NodeManager/install_libzmq.sh

For additional information, refer to http://openbts.org.


These are the directories:

AsteriskConfig	Asterisk configuration files for use with OpenBTS.
CommonLib	Common-use libraries, mostly C++ wrappers for basic facilities.
Control		Control-layer functions for the protocols of GSM 04.08 and SIP.
GSM		The GSM stack.
RRLP		Radio Resource Location Protocol
SIP		Components of the SIP state machines ued by the control layer.
SMS		The SMS stack.
SR		The subscriber registry.
TRXManager	The interface between the GSM stack and the radio.
Transceiver	The software transceiver and specific installation tests.
apps		OpenBTS application binaries.
doc		Project documentation.
tests		Test fixtures for subsets of OpenBTS components.
smqueue		RFC-3428 store-and-forward server for SMS



By default, OpenBTS assumes the following UDP port assignments:

5060 -- Asterisk SIP interface
5061 -- local SIP softphone
5062 -- OpenBTS SIP interface
5063 -- smqueue SIP interface
5064 -- subscriber registry SIP interface
5700-range -- OpenBTS-transceiver interface

These can be controlled in the CONFIG table in /etc/OpenBTS.db.

Standrd paths:
/OpenBTS -- Binary installation and authorization keys.
/etc/OpenBTS -- Configuration databases.
/var/run/ -- Real-time reporting databases.

The script apps/setUpFiles.sh will create these directories and install the
correct files in them.


Releases 2.5 and later include the smqueue SMS server.  It is NOT part of the
normal GNU build process with the rest of OpenBTS.  To build smqueue, go
into the smqueue directory and just type "make -f Makefile.standalone".


# Release history:

Release	Name		SVN Reposiory	SVN Rev	Comments

1.0	(none)		SF.net		??		completed L1, L2

1.1	Arnaudville	GNU Radio	r10019 (trunk)

1.2	Breaux Bridge	GNU Radio	r10088 (trunk)	GNU Build, very early assignment

1.3	Carencro	KSP		r1 (trunk)	first post-injunction release

1.4	Donaldsonville	KSP		r23 (trunk)	fixed Ubuntu build error

1.5	Eunice		KSP		r39 (trunk)	fixed L2 bugs related to segmentation
							removed incomplete SMS directory
							moved "abort" calls into L3 subclasses

1.6	New Iberia	KSP		r130 (trunk)	import of all 2.2 improvements to non-SMS release


2.0	St. Francisville KSP		r54 (smswork)	SMS support
							file-based configuration

2.1	Grand Coteau	KSP		r70 (smswork)	DTMF support
							fixed more Linux-related build errors
								-lpthread
								TLMessage constructor
							expanded stack to prevent overflows in Linux
							moved gSIPInterface to main app
							fixed iterator bug in Pager

2.2	Houma		KSP		r122 (smswork)	added LEGAL notice
							removed Assert classes
							stop paging on page response
							fixed Pager-spin bug
							fixed Transceiver spin bugs
							fixed 2^32 microsecond rollover bug
							reduced stack footprints in Transceiver
							fixed SMS timestamps
							check LAI before using TMSI in LUR
							reduced memory requirement by 75%
							removed PagerTest
							fixed stale-transaction bug in paging handler
							fixed USRP clock rollover bug
							faster call connection
							new USRPDevice design

2.3	Jean Lafitte	KSP		r190? (trunk)	check for out-of-date RACH bursts
							better TRX-GSM clock sync
							formal logging system
							command line interface
							emergency call setup

2.4	Kinder		KSP		r208? (trunk)	fixed BCCH neighbor list bug
							support for neighbor lists
							fixed support for non-local Asterisk servers
							cleaner configuration management
							more realtime control of BCCH parameters
							proper rejection of Hold messages
							fixed L3 hanging bug in MTDCheckBYE

2.4.1	Kinder		KSP		r462		fixed lots of valgrind errors

2.4.2	Kinder		KSP		r482		zero-length calling party number bug
							g++ 4.4 #includes

2.5	Lacassine	KSP		r551		imported Joshua Lackey patches
							SIP fixes from Anne Kwong
							SIP fixes from testing with SMS server
							L3 TI handling fixes
							SMS server support
							GNU Radio 3.2 compatibility
							configurable max range and LU-reject cause
							"page" & "testcall" CLI features

2.5.1	Lacassine	KSP		r595		fixed some build bugs for some Linux distros

2.5.2	Lacassine	KSP		r630		fixed channel assignment bug for Nokia DCT4+ handsets

2.5.3	Lacassine	KSP		r756		merged fix for transceiver startup crash
								due to use of uninitialized variables (r646)
							merged fix for fusb bug from trunk (r582)

2.5.4	Lacassine	KSP		r812		merged fixes to build under latest Fedora and
								to build with git GnuRadio (r814)

2.6	Mamou		KSP		r886		fixed infamous fusb bug (r582)
							fixed idle-filling table size bug
							smoother uplink power control
							load-limiting downlink power control
							new "config" features (optional, static)
							IMEI interrogation
							fixed MOD "missing FIFO" bug
							configurable short code features
							fixed transceiver startup crash (r646)
							readline support is back
							fixed timing advance bug (r844)
							added CLI "chans" command
							track time-of-use in TMSI table (r844)
							added CLI "noise" command (r844)
							added CLI "rxpower" command (r844)
							added CLI "unconfig" command

2.7	Natchitoches	Range	rxxx			converted TMSITable to sqlite3 (r902)
							sqlite3-based configuration (r???)
							converted Logger to syslogd (r903)
							added support for rest octets (r1022)
							external database for transaction reporting (r1184)
							external database for channel status reporting (r1203)
							in-call delivery and submission of text messages (r1231)
							RFC-2833 DMTF (r1249)

2.8	Opelousas	Range	rxxx			added SHA1/RSA image verification
							move databases to /etc and /var
							SIP-based authentication

2.9	Plaquemine	Range				socket-based remote CLI
							merge-in of "S" Release
5.0     ?                ?                              ?
5.1     FlUxIuS         Penthertz                       Release for 2023 compiling with fresh Ubuntu 22.04
