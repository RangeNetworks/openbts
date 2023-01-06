#!/bin/sh

##########################################################
# This is a simple script made by FlUxIuS @ Penthertz    #
# It is there to simplify the installation of this whole #
# project.                                               #
##########################################################

# Installing compilation dependencies
sudo apt install autoconf libtool libosip2-dev libortp-dev libusb-1.0-0-dev g++ sqlite3 libsqlite3-dev erlang libreadline6-dev libncurses5-dev git dpkg-dev debhelper libssl-dev cmake build-essential wget libzmq3-dev

# Installing HD driver 4.1.0 & tools from Ubuntu package manager
sudo apt install libuhd4.1.0 libuhd-dev uhd-host

# Cloning submodules
git submodule init
git submodule update

# Installing libcoredumper
git clone https://github.com/PentHertz/libcoredumper.git
cd libcoredumper
./build.sh
cd coredumper-1.2.1
./configure
make -j$(nproc)
sudo make install
sudo ldconfig
cd  ../..

# Installing subscriberRegistry
git clone https://github.com/PentHertz/subscriberRegistry.git
cd subscriberRegistry
git submodule init
git submodule update
./autogen.sh
./configure
make -j$(nproc)
sudo make install
sudo ldconfig
cd ../

# Installing liba53
git clone https://github.com/PentHertz/liba53.git
cd liba53
make && make install
sudo ldconfig
cd ..

# Installing smqueue
git clone https://github.com/PentHertz/smqueue.git
cd smqueue
git submodule init
git submodule update
./autogen.sh
./configure
make -j$(nproc)
sudo make install
sudo ldconfig
cd ../

# Making rooms for subscriberRegistry and smqueue
sudo sqlite3 -init ./apps/OpenBTS.example.sql /etc/OpenBTS/OpenBTS.db ".quit"
sudo mkdir -p /var/lib/asterisk/sqlite3dir
sudo sqlite3 -init /etc/OpenBTS/sipauthserve.example.sql /etc/OpenBTS/sipauthserve.db ".quit"
sudo mkdir /var/lib/OpenBTS
sudo touch /var/lib/OpenBTS/smq.cdr

# Installing OpenBTS
#./autogen.sh
#./configure --with-uhd
#make -j$(nproc)
#make install
