#!/bin/sh

# Installing compilation dependencies
sudo apt install autoconf libtool libosip2-dev libortp-dev libusb-1.0-0-dev g++ sqlite3 \ 
	libsqlite3-dev erlang libreadline6-dev libncurses5-dev git dpkg-dev debhelper \ 
	libssl-dev cmake build-essential wget libzmq3-dev

# Installing HD driver 4.1.0 & tools from Ubuntu package manager
sudo apt install libuhd4.1.0 libuhd-dev uhd-host

# Cloning submodules
git submodule init
git submodule update

# Installing libcoredumper
git clone https://github.com/PentHertz/libcoredumper.git
cd libcoredumper
./build
cd coredumper-1.2.1
./configure
make -j$(nproc)
sudo make install
cd  ../..

# Installing subscriberRegistry
git clone https://github.com/PentHertz/subscriberRegistry.git
cd subscriberRegistry
git submodule init
git submodule update
./autogen.sh
./configure
make -j$(nproc)
cd ../

# Installing liba53
git clone https://github.com/PentHertz/liba53.git
cd liba53
make && make install
cd ..

# Installing OpenBTS
#./autogen.sh
#./configure --with-uhd
#make -j$(nproc)
#make install
