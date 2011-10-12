/*
* Copyright 2011 Free Software Foundation, Inc.
*
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/*
 * We've now split the RAD1 into 3 separate interfaces.
 *
 * Interface 0 contains only ep0 and is used for command and status.
 * Interface 1 is the Tx path and it uses ep2 OUT BULK.
 * Interface 2 is the Rx path and it uses ep6 IN BULK.
 */

#define RAD1_CMD_INTERFACE              0
#define RAD1_CMD_ALTINTERFACE           0
#define RAD1_CMD_ENDPOINT               0

#define RAD1_TX_INTERFACE               1
#define RAD1_TX_ALTINTERFACE            0
#define RAD1_TX_ENDPOINT                2       // streaming data from host to FPGA

#define RAD1_RX_INTERFACE               2
#define RAD1_RX_ALTINTERFACE            0
#define RAD1_RX_ENDPOINT                6       // streaming data from FPGA to host

