/*
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/


#include <iostream>
#include <sstream>
#include <cstdlib>
#include <zmq.hpp>

int main(int argc, char **argv)
{
	zmq::context_t context(4);
	zmq::socket_t targetPublisher(context, ZMQ_SUB);
	std::string localopenbts = "tcp://127.0.0.1:45160";

	targetPublisher.setsockopt(ZMQ_SUBSCRIBE, "", 0);
	targetPublisher.connect(localopenbts.c_str());
	while (1) {
		try {
			zmq::message_t event;
			targetPublisher.recv(&event);
			std::cout << std::string(static_cast<char*>(event.data()), event.size()) << std::endl;

		} catch(const zmq::error_t& e) {
			std::cout << "!! exception !!" << std::endl;
		}
	}
}
