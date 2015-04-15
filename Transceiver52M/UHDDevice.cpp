/*
 * Device support for Ettus Research UHD driver 
 * Written by Thomas Tsou <ttsou@vt.edu>
 *
 * Copyright 2010,2011 Free Software Foundation, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * See the COPYING file in the main directory for details.
 */

#include "radioDevice.h"
#include "Threads.h"
#include "Logger.h"
#include <uhd/version.hpp>
#include <uhd/property_tree.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/msg.hpp>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define B2XX_CLK_RT      26e6
#define B100_BASE_RT     400000
#define USRP2_BASE_RT    390625
#define TX_AMPL          0.3
#define SAMPLE_BUF_SZ    (1 << 20)

enum uhd_dev_type {
	USRP1,
	USRP2,
	B100,
	B2XX,
	X3XX,
	UMTRX,
	NUM_USRP_TYPES,
};

struct uhd_dev_offset {
	enum uhd_dev_type type;
	int sps;
	double offset;
	const std::string desc;
};

/*
 * USRP version dependent device timings
 */
#ifdef USE_UHD_3_9
#define B2XX_TIMING_1SPS	1.7153e-4
#define B2XX_TIMING_4SPS	1.1696e-4
#else
#define B2XX_TIMING_1SPS	9.9692e-5
#define B2XX_TIMING_4SPS	6.9248e-5
#endif

/*
 * Tx / Rx sample offset values. In a perfect world, there is no group delay
 * though analog components, and behaviour through digital filters exactly
 * matches calculated values. In reality, there are unaccounted factors,
 * which are captured in these empirically measured (using a loopback test)
 * timing correction values.
 *
 * Notes:
 *   USRP1 with timestamps is not supported by UHD.
 */
static struct uhd_dev_offset uhd_offsets[NUM_USRP_TYPES * 2] = {
	{ USRP1, 1,       0.0, "USRP1 not supported" },
	{ USRP1, 4,       0.0, "USRP1 not supported"},
	{ USRP2, 1, 1.2184e-4, "N2XX 1 SPS" },
	{ USRP2, 4, 8.0230e-5, "N2XX 4 SPS" },
	{ B100,  1, 1.2104e-4, "B100 1 SPS" },
	{ B100,  4, 7.9307e-5, "B100 4 SPS" },
	{ B2XX,  1, B2XX_TIMING_1SPS, "B200 1 SPS" },
	{ B2XX,  4, B2XX_TIMING_4SPS, "B200 4 SPS" },
	{ X3XX,  1, 1.5360e-4, "X3XX 1 SPS"},
	{ X3XX,  4, 1.1264e-4, "X3XX 4 SPS"},
	{ UMTRX, 1, 9.9692e-5, "UmTRX 1 SPS" },
	{ UMTRX, 4, 7.3846e-5, "UmTRX 4 SPS" },
};

static double get_dev_offset(enum uhd_dev_type type, int sps)
{
	if (type == USRP1) {
		LOG(ERR) << "Invalid device type";
		return 0.0;
	}

	switch (sps) {
	case 1:
		return uhd_offsets[2 * type + 0].offset;
	case 4:
		return uhd_offsets[2 * type + 1].offset;
	}

	LOG(ERR) << "Unsupported samples-per-symbols: " << sps;
	return 0.0;
}

/*
 * Select sample rate based on device type and requested samples-per-symbol.
 * The base rate is either GSM symbol rate, 270.833 kHz, or the minimum
 * usable channel spacing of 400 kHz.
 */
static double select_rate(uhd_dev_type type, int sps)
{
	if ((sps != 4) && (sps != 1))
		return -9999.99;

	switch (type) {
	case USRP2:
	case X3XX:
		return USRP2_BASE_RT * sps;
	case B100:
		return B100_BASE_RT * sps;
	case B2XX:
	case UMTRX:
		return GSMRATE * sps;
	default:
		break;
	}

	LOG(ALERT) << "Unknown device type " << type;
	return -9999.99;
}

/*
    Sample Buffer - Allows reading and writing of timed samples using OpenBTS
                    or UHD style timestamps.
*/
class smpl_buf {
public:
	/** Sample buffer constructor
	    @param len number of 32-bit samples the buffer should hold
	    @param rate sample clockrate 
	    @param timestamp 
	*/
	smpl_buf(size_t len, double rate);
	~smpl_buf();

	/** Query number of samples available for reading
	    @param timestamp time of first sample
	    @return number of available samples or error
	*/
	ssize_t avail_smpls(TIMESTAMP timestamp) const;
	ssize_t avail_smpls(uhd::time_spec_t timestamp) const;

	/** Read and write
	    @param buf pointer to buffer
	    @param len number of samples desired to read or write
	    @param timestamp time of first stample
	    @return number of actual samples read or written or error
	*/
	ssize_t read(void *buf, size_t len, TIMESTAMP timestamp);
	ssize_t read(void *buf, size_t len, uhd::time_spec_t timestamp);
	ssize_t write(void *buf, size_t len, TIMESTAMP timestamp);
	ssize_t write(void *buf, size_t len, uhd::time_spec_t timestamp);

	/** Buffer status string
	    @return a formatted string describing internal buffer state
	*/
	std::string str_status() const;

	/** Formatted error string 
	    @param code an error code
	    @return a formatted error string
	*/
	static std::string str_code(ssize_t code);

	enum err_code {
		ERROR_TIMESTAMP = -1,
		ERROR_READ = -2,
		ERROR_WRITE = -3,
		ERROR_OVERFLOW = -4
	};

private:
	uint32_t *data;
	size_t buf_len;

	double clk_rt;

	TIMESTAMP time_start;
	TIMESTAMP time_end;

	size_t data_start;
	size_t data_end;
};

/*
    uhd_device - UHD implementation of the Device interface. Timestamped samples
                are sent to and received from the device. An intermediate buffer
                on the receive side collects and aligns packets of samples.
                Events and errors such as underruns are reported asynchronously
                by the device and received in a separate thread.
*/
class uhd_device : public RadioDevice {
public:
	uhd_device(int sps, bool skip_rx);
	~uhd_device();

	int open(const std::string &args, bool extref);
	bool start();
	bool stop();
	void restart(uhd::time_spec_t ts);
	void setPriority();
	enum TxWindowType getWindowType() { return tx_window; }

	int readSamples(short *buf, int len, bool *overrun, 
			TIMESTAMP timestamp, bool *underrun, unsigned *RSSI);

	int writeSamples(short *buf, int len, bool *underrun, 
			 TIMESTAMP timestamp, bool isControl);

	bool updateAlignment(TIMESTAMP timestamp);

	bool setTxFreq(double wFreq);
	bool setRxFreq(double wFreq);

	inline TIMESTAMP initialWriteTimestamp() { return 0; }
	inline TIMESTAMP initialReadTimestamp() { return 0; }

	inline double fullScaleInputValue() { return 32000 * TX_AMPL; }
	inline double fullScaleOutputValue() { return 32000; }

	double setRxGain(double db);
	double getRxGain(void) { return rx_gain; }
	double maxRxGain(void) { return rx_gain_max; }
	double minRxGain(void) { return rx_gain_min; }

	double setTxGain(double db);
	double maxTxGain(void) { return tx_gain_max; }
	double minTxGain(void) { return tx_gain_min; }

	double getTxFreq() { return tx_freq; }
	double getRxFreq() { return rx_freq; }

	inline double getSampleRate() { return tx_rate; }
	inline double numberRead() { return rx_pkt_cnt; }
	inline double numberWritten() { return 0; }

	/** Receive and process asynchronous message
	    @return true if message received or false on timeout or error
	*/
	bool recv_async_msg();

	enum err_code {
		ERROR_TIMING = -1,
		ERROR_UNRECOVERABLE = -2,
		ERROR_UNHANDLED = -3,
	};

private:
	uhd::usrp::multi_usrp::sptr usrp_dev;
	uhd::tx_streamer::sptr tx_stream;
	uhd::rx_streamer::sptr rx_stream;
	enum TxWindowType tx_window;
	enum uhd_dev_type dev_type;

	int sps;
	double tx_rate, rx_rate;

	double tx_gain, tx_gain_min, tx_gain_max;
	double rx_gain, rx_gain_min, rx_gain_max;

	double tx_freq, rx_freq;
	size_t tx_spp, rx_spp;

	bool started;
	bool aligned;
	bool skip_rx;

	size_t rx_pkt_cnt;
	size_t drop_cnt;
	uhd::time_spec_t prev_ts;

	TIMESTAMP ts_offset;
	smpl_buf *rx_smpl_buf;

	void init_gains();
	void set_ref_clk(bool ext_clk);
	int set_master_clk(double rate);
	int set_rates(double tx_rate, double rx_rate);
	bool parse_dev_type();
	bool flush_recv(size_t num_pkts);
	int check_rx_md_err(uhd::rx_metadata_t &md, ssize_t num_smpls);

	std::string str_code(uhd::rx_metadata_t metadata);
	std::string str_code(uhd::async_metadata_t metadata);

	Thread async_event_thrd;
};

void *async_event_loop(uhd_device *dev)
{
	while (1) {
		dev->recv_async_msg();
		pthread_testcancel();
	}

	return NULL;
}

/* 
    Catch and drop underrun 'U' and overrun 'O' messages from stdout
    since we already report using the logging facility. Direct
    everything else appropriately.
 */
void uhd_msg_handler(uhd::msg::type_t type, const std::string &msg)
{
	switch (type) {
	case uhd::msg::status:
		LOG(INFO) << msg;
		break;
	case uhd::msg::warning:
		LOG(WARNING) << msg;
		break;
	case uhd::msg::error:
		LOG(ERR) << msg;
		break;
	case uhd::msg::fastpath:
		break;
	}
}

uhd_device::uhd_device(int sps, bool skip_rx)
	: tx_gain(0.0), tx_gain_min(0.0), tx_gain_max(0.0),
	  rx_gain(0.0), rx_gain_min(0.0), rx_gain_max(0.0),
	  tx_freq(0.0), rx_freq(0.0), tx_spp(0), rx_spp(0),
	  started(false), aligned(false), rx_pkt_cnt(0), drop_cnt(0),
	  prev_ts(0,0), ts_offset(0), rx_smpl_buf(NULL)
{
	this->sps = sps;
	this->skip_rx = skip_rx;
}

uhd_device::~uhd_device()
{
	stop();

	if (rx_smpl_buf)
		delete rx_smpl_buf;
}

void uhd_device::init_gains()
{
	uhd::gain_range_t range;

	range = usrp_dev->get_tx_gain_range();
	tx_gain_min = range.start();
	tx_gain_max = range.stop();

	range = usrp_dev->get_rx_gain_range();
	rx_gain_min = range.start();
	rx_gain_max = range.stop();

	usrp_dev->set_tx_gain((tx_gain_min + tx_gain_max) / 2);
	usrp_dev->set_rx_gain((rx_gain_min + rx_gain_max) / 2);

	tx_gain = usrp_dev->get_tx_gain();
	rx_gain = usrp_dev->get_rx_gain();

	return;
}

void uhd_device::set_ref_clk(bool ext_clk)
{
	if (ext_clk)
		usrp_dev->set_clock_source("external");

	return;
}

int uhd_device::set_master_clk(double clk_rate)
{
	double actual, offset, limit = 1.0;

	try {
		usrp_dev->set_master_clock_rate(clk_rate);
	} catch (const std::exception &ex) {
		LOG(ALERT) << "UHD clock rate setting failed: " << clk_rate;
		LOG(ALERT) << ex.what();
		return -1;
	}

	actual = usrp_dev->get_master_clock_rate();
	offset = fabs(clk_rate - actual);

	if (offset > limit) {
		LOG(ALERT) << "Failed to set master clock rate";
		LOG(ALERT) << "Requested clock rate " << clk_rate;
		LOG(ALERT) << "Actual clock rate " << actual;
		return -1;
	}

	return 0;
}

int uhd_device::set_rates(double tx_rate, double rx_rate)
{
	double offset_limit = 1.0;
	double tx_offset, rx_offset;

	// B2XX is the only device where we set FPGA clocking
	if (dev_type == B2XX) {
		if (set_master_clk(B2XX_CLK_RT) < 0)
			return -1;
	}

	// Set sample rates
	try {
		usrp_dev->set_tx_rate(tx_rate);
		usrp_dev->set_rx_rate(rx_rate);
	} catch (const std::exception &ex) {
		LOG(ALERT) << "UHD rate setting failed";
		LOG(ALERT) << ex.what();
		return -1;
	}
	this->tx_rate = usrp_dev->get_tx_rate();
	this->rx_rate = usrp_dev->get_rx_rate();

	tx_offset = fabs(this->tx_rate - tx_rate);
	rx_offset = fabs(this->rx_rate - rx_rate);
	if ((tx_offset > offset_limit) || (rx_offset > offset_limit)) {
		LOG(ALERT) << "Actual sample rate differs from desired rate";
		LOG(ALERT) << "Tx/Rx (" << this->tx_rate << "/"
			   << this->rx_rate << ")";
		return -1;
	}

	return 0;
}

double uhd_device::setTxGain(double db)
{
	usrp_dev->set_tx_gain(db);
	tx_gain = usrp_dev->get_tx_gain();

	LOG(INFO) << "Set TX gain to " << tx_gain << "dB";

	return tx_gain;
}

double uhd_device::setRxGain(double db)
{
	usrp_dev->set_rx_gain(db);
	rx_gain = usrp_dev->get_rx_gain();

	LOG(INFO) << "Set RX gain to " << rx_gain << "dB";

	return rx_gain;
}

/*
    Parse the UHD device tree and mboard name to find out what device we're
    dealing with. We need the window type so that the transceiver knows how to
    deal with the transport latency. Reject the USRP1 because UHD doesn't
    support timestamped samples with it.
 */
bool uhd_device::parse_dev_type()
{
	std::string mboard_str, dev_str;
	uhd::property_tree::sptr prop_tree;
	size_t usrp1_str, usrp2_str, b100_str, b200_str,
	       b210_str, x300_str, x310_str, umtrx_str;

	prop_tree = usrp_dev->get_device()->get_tree();
	dev_str = prop_tree->access<std::string>("/name").get();
	mboard_str = usrp_dev->get_mboard_name();

	usrp1_str = dev_str.find("USRP1");
	usrp2_str = dev_str.find("USRP2");
	b100_str = mboard_str.find("B100");
	b200_str = mboard_str.find("B200");
	b210_str = mboard_str.find("B210");
	x300_str = mboard_str.find("X300");
	x310_str = mboard_str.find("X310");
	umtrx_str = dev_str.find("UmTRX");

	if (usrp1_str != std::string::npos) {
		LOG(ALERT) << "USRP1 is not supported using the UHD driver";
		LOG(ALERT) << "Please compile with GNU Radio libusrp support";
		dev_type = USRP1;
		return false;
	}

	if (b100_str != std::string::npos) {
		tx_window = TX_WINDOW_USRP1;
		LOG(INFO) << "Using USRP1 type transmit window for "
			  << dev_str << " " << mboard_str;
		dev_type = B100;
		return true;
	} else if (b200_str != std::string::npos) {
		dev_type = B2XX;
	} else if (b210_str != std::string::npos) {
		dev_type = B2XX;
	} else if (x300_str != std::string::npos) {
		dev_type = X3XX;
	} else if (x310_str != std::string::npos) {
		dev_type = X3XX;
	} else if (usrp2_str != std::string::npos) {
		dev_type = USRP2;
	} else if (umtrx_str != std::string::npos) {
		dev_type = UMTRX;
	} else {
		LOG(ALERT) << "Unknown UHD device type " << dev_str;
		return false;
	}

	tx_window = TX_WINDOW_FIXED;
	LOG(INFO) << "Using fixed transmit window for "
		  << dev_str << " " << mboard_str;
	return true;
}

int uhd_device::open(const std::string &args, bool extref)
{
	// Find UHD devices
	uhd::device_addr_t addr(args);
	uhd::device_addrs_t dev_addrs = uhd::device::find(addr);
	if (dev_addrs.size() == 0) {
		LOG(ALERT) << "No UHD devices found with address '" << args << "'";
		return -1;
	}

	// Use the first found device
	LOG(INFO) << "Using discovered UHD device " << dev_addrs[0].to_string();
	try {
		usrp_dev = uhd::usrp::multi_usrp::make(dev_addrs[0]);
	} catch(...) {
		LOG(ALERT) << "UHD make failed, device " << dev_addrs[0].to_string();
		return -1;
	}

	// Check for a valid device type and set bus type
	if (!parse_dev_type())
		return -1;

	if (extref)
		set_ref_clk(true);

	// Create TX and RX streamers
	uhd::stream_args_t stream_args("sc16");
	tx_stream = usrp_dev->get_tx_stream(stream_args);
	rx_stream = usrp_dev->get_rx_stream(stream_args);

	// Number of samples per over-the-wire packet
	tx_spp = tx_stream->get_max_num_samps();
	rx_spp = rx_stream->get_max_num_samps();

	// Set rates
	double _tx_rate = select_rate(dev_type, sps);
	double _rx_rate = _tx_rate / sps;
	if ((_tx_rate > 0.0) && (set_rates(_tx_rate, _rx_rate) < 0))
		return -1;

	// Create receive buffer
	size_t buf_len = SAMPLE_BUF_SZ / sizeof(uint32_t);
	rx_smpl_buf = new smpl_buf(buf_len, rx_rate);

	// Set receive chain sample offset 
	double offset = get_dev_offset(dev_type, sps);
	if (offset == 0.0) {
		LOG(ERR) << "Unsupported configuration, no correction applied";
		ts_offset = 0;
	} else  {
		ts_offset = (TIMESTAMP) (offset * rx_rate);
	}

	// Initialize and shadow gain values 
	init_gains();

	// Print configuration
	LOG(INFO) << "\n" << usrp_dev->get_pp_string();

	switch (dev_type) {
	case B100:
		return RESAMP_64M;
	case USRP2:
	case X3XX:
		return RESAMP_100M;
	case B2XX:
	case UMTRX:
	default:
		return NORMAL;
	}
}

bool uhd_device::flush_recv(size_t num_pkts)
{
	uhd::rx_metadata_t md;
	size_t num_smpls;
	uint32_t buff[rx_spp];
	float timeout;

	// Use .01 sec instead of the default .1 sec
	timeout = .01;

	for (size_t i = 0; i < num_pkts; i++) {
		num_smpls = rx_stream->recv(buff, rx_spp, md,
					    timeout, true);
		if (!num_smpls) {
			switch (md.error_code) {
			case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
				return true;
			default:
				continue;
			}
		}
	}

	return true;
}

void uhd_device::restart(uhd::time_spec_t ts)
{
	uhd::stream_cmd_t cmd = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
	rx_stream->issue_stream_cmd(cmd);

	flush_recv(50);

	usrp_dev->set_time_now(ts);
	aligned = false;

	cmd = uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS;
	cmd.stream_now = true;
	rx_stream->issue_stream_cmd(cmd);
}

bool uhd_device::start()
{
	LOG(INFO) << "Starting USRP...";

	if (started) {
		LOG(ERR) << "Device already started";
		return false;
	}

	setPriority();

	// Register msg handler
	uhd::msg::register_handler(&uhd_msg_handler);

	// Start asynchronous event (underrun check) loop
	async_event_thrd.start((void * (*)(void*))async_event_loop, (void*)this);

	// Start streaming
	restart(uhd::time_spec_t(0.0));

	// Display usrp time
	double time_now = usrp_dev->get_time_now().get_real_secs();
	LOG(INFO) << "The current time is " << time_now << " seconds";

	started = true;
	return true;
}

bool uhd_device::stop()
{
	uhd::stream_cmd_t stream_cmd = 
		uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;

	usrp_dev->issue_stream_cmd(stream_cmd);

	started = false;
	return true;
}

void uhd_device::setPriority()
{
	uhd::set_thread_priority_safe();
	return;
}

int uhd_device::check_rx_md_err(uhd::rx_metadata_t &md, ssize_t num_smpls)
{
	uhd::time_spec_t ts;

	if (!num_smpls) {
		LOG(ERR) << str_code(md);

		switch (md.error_code) {
		case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
			LOG(ALERT) << "UHD: Receive timed out";
			return ERROR_UNRECOVERABLE;
		case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
		case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND:
		case uhd::rx_metadata_t::ERROR_CODE_BROKEN_CHAIN:
		case uhd::rx_metadata_t::ERROR_CODE_BAD_PACKET:
		default:
			return ERROR_UNHANDLED;
		}
	}

	// Missing timestamp
	if (!md.has_time_spec) {
		LOG(ALERT) << "UHD: Received packet missing timestamp";
		return ERROR_UNRECOVERABLE;
	}

	ts = md.time_spec;

	// Monotonicity check
	if (ts < prev_ts) {
		LOG(ALERT) << "UHD: Loss of monotonic time";
		LOG(ALERT) << "Current time: " << ts.get_real_secs() << ", " 
			   << "Previous time: " << prev_ts.get_real_secs();
		return ERROR_TIMING;
	} else {
		prev_ts = ts;
	}

	return 0;
}

int uhd_device::readSamples(short *buf, int len, bool *overrun,
			TIMESTAMP timestamp, bool *underrun, unsigned *RSSI)
{
	ssize_t rc;
	uhd::time_spec_t ts;
	uhd::rx_metadata_t metadata;
	uint32_t pkt_buf[rx_spp];

	if (skip_rx)
		return 0;

	*overrun = false;
	*underrun = false;

	// Shift read time with respect to transmit clock
	timestamp += ts_offset;

	ts = uhd::time_spec_t::from_ticks(timestamp, rx_rate);
	LOG(DEBUG) << "Requested timestamp = " << ts.get_real_secs();

	// Check that timestamp is valid
	rc = rx_smpl_buf->avail_smpls(timestamp);
	if (rc < 0) {
		LOG(ERR) << rx_smpl_buf->str_code(rc);
		LOG(ERR) << rx_smpl_buf->str_status();
		return 0;
	}

	// Receive samples from the usrp until we have enough
	while (rx_smpl_buf->avail_smpls(timestamp) < len) {
		size_t num_smpls = rx_stream->recv(
					(void*)pkt_buf,
					rx_spp,
					metadata,
					0.1,
					true);
		rx_pkt_cnt++;

		// Check for errors 
		rc = check_rx_md_err(metadata, num_smpls);
		switch (rc) {
		case ERROR_UNRECOVERABLE:
			LOG(ALERT) << "UHD: Version " << uhd::get_version_string();
			LOG(ALERT) << "UHD: Unrecoverable error, exiting...";
			exit(-1);
		case ERROR_TIMING:
			restart(prev_ts);
		case ERROR_UNHANDLED:
			continue;
		}


		ts = metadata.time_spec;
		LOG(DEBUG) << "Received timestamp = " << ts.get_real_secs();

		rc = rx_smpl_buf->write(pkt_buf,
					num_smpls,
					metadata.time_spec);

		// Continue on local overrun, exit on other errors
		if ((rc < 0)) {
			LOG(ERR) << rx_smpl_buf->str_code(rc);
			LOG(ERR) << rx_smpl_buf->str_status();
			if (rc != smpl_buf::ERROR_OVERFLOW)
				return 0;
		}
	}

	// We have enough samples
	rc = rx_smpl_buf->read(buf, len, timestamp);
	if ((rc < 0) || (rc != len)) {
		LOG(ERR) << rx_smpl_buf->str_code(rc);
		LOG(ERR) << rx_smpl_buf->str_status();
		return 0;
	}

	return len;
}

int uhd_device::writeSamples(short *buf, int len, bool *underrun,
			unsigned long long timestamp,bool isControl)
{
	uhd::tx_metadata_t metadata;
	metadata.has_time_spec = true;
	metadata.start_of_burst = false;
	metadata.end_of_burst = false;
	metadata.time_spec = uhd::time_spec_t::from_ticks(timestamp, tx_rate);

	*underrun = false;

	// No control packets
	if (isControl) {
		LOG(ERR) << "Control packets not supported";
		return 0;
	}

	// Drop a fixed number of packets (magic value)
	if (!aligned) {
		drop_cnt++;

		if (drop_cnt == 1) {
			LOG(DEBUG) << "Aligning transmitter: stop burst";
			*underrun = true;
			metadata.end_of_burst = true;
		} else if (drop_cnt < 30) {
			LOG(DEBUG) << "Aligning transmitter: packet advance";
			return len;
		} else {
			LOG(DEBUG) << "Aligning transmitter: start burst";
			metadata.start_of_burst = true;
			aligned = true;
			drop_cnt = 0;
		}
	}

	size_t num_smpls = tx_stream->send(buf, len, metadata);

	if (num_smpls != (unsigned) len) {
		LOG(ALERT) << "UHD: Device send timed out";
		LOG(ALERT) << "UHD: Version " << uhd::get_version_string();
		LOG(ALERT) << "UHD: Unrecoverable error, exiting...";
		exit(-1);
	}

	return num_smpls;
}

bool uhd_device::updateAlignment(TIMESTAMP timestamp)
{
	return true;
}

bool uhd_device::setTxFreq(double wFreq)
{
	uhd::tune_result_t tr = usrp_dev->set_tx_freq(wFreq);
	LOG(INFO) << "\n" << tr.to_pp_string();
	tx_freq = usrp_dev->get_tx_freq();

	return true;
}

bool uhd_device::setRxFreq(double wFreq)
{
	uhd::tune_result_t tr = usrp_dev->set_rx_freq(wFreq);
	LOG(INFO) << "\n" << tr.to_pp_string();
	rx_freq = usrp_dev->get_rx_freq();

	return true;
}

bool uhd_device::recv_async_msg()
{
	uhd::async_metadata_t md;
	if (!tx_stream->recv_async_msg(md))
		return false;

	// Assume that any error requires resynchronization
	if (md.event_code != uhd::async_metadata_t::EVENT_CODE_BURST_ACK) {
		aligned = false;

		if ((md.event_code != uhd::async_metadata_t::EVENT_CODE_UNDERFLOW) &&
		    (md.event_code != uhd::async_metadata_t::EVENT_CODE_TIME_ERROR)) {
			LOG(ERR) << str_code(md);
		}
	}

	return true;
}

std::string uhd_device::str_code(uhd::rx_metadata_t metadata)
{
	std::ostringstream ost("UHD: ");

	switch (metadata.error_code) {
	case uhd::rx_metadata_t::ERROR_CODE_NONE:
		ost << "No error";
		break;
	case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
		ost << "No packet received, implementation timed-out";
		break;
	case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND:
		ost << "A stream command was issued in the past";
		break;
	case uhd::rx_metadata_t::ERROR_CODE_BROKEN_CHAIN:
		ost << "Expected another stream command";
		break;
	case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
		ost << "An internal receive buffer has filled";
		break;
	case uhd::rx_metadata_t::ERROR_CODE_BAD_PACKET:
		ost << "The packet could not be parsed";
		break;
	default:
		ost << "Unknown error " << metadata.error_code;
	}

	if (metadata.has_time_spec)
		ost << " at " << metadata.time_spec.get_real_secs() << " sec.";

	return ost.str();
}

std::string uhd_device::str_code(uhd::async_metadata_t metadata)
{
	std::ostringstream ost("UHD: ");

	switch (metadata.event_code) {
	case uhd::async_metadata_t::EVENT_CODE_BURST_ACK:
		ost << "A packet was successfully transmitted";
		break;
	case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW:
		ost << "An internal send buffer has emptied";
		break;
	case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR:
		ost << "Packet loss between host and device";
		break;
	case uhd::async_metadata_t::EVENT_CODE_TIME_ERROR:
		ost << "Packet time was too late or too early";
		break;
	case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET:
		ost << "Underflow occurred inside a packet";
		break;
	case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR_IN_BURST:
		ost << "Packet loss within a burst";
		break;
	default:
		ost << "Unknown error " << metadata.event_code;
	}

	if (metadata.has_time_spec)
		ost << " at " << metadata.time_spec.get_real_secs() << " sec.";

	return ost.str();
}

smpl_buf::smpl_buf(size_t len, double rate)
	: buf_len(len), clk_rt(rate),
	  time_start(0), time_end(0), data_start(0), data_end(0)
{
	data = new uint32_t[len];
}

smpl_buf::~smpl_buf()
{
	delete[] data;
}

ssize_t smpl_buf::avail_smpls(TIMESTAMP timestamp) const
{
	if (timestamp < time_start)
		return ERROR_TIMESTAMP;
	else if (timestamp >= time_end)
		return 0;
	else
		return time_end - timestamp;
}

ssize_t smpl_buf::avail_smpls(uhd::time_spec_t ts) const
{
	return avail_smpls(ts.to_ticks(clk_rt));
}

ssize_t smpl_buf::read(void *buf, size_t len, TIMESTAMP timestamp)
{
	int type_sz = 2 * sizeof(short);

	// Check for valid read
	if (timestamp < time_start)
		return ERROR_TIMESTAMP;
	if (timestamp >= time_end)
		return 0;
	if (len >= buf_len)
		return ERROR_READ;

	// How many samples should be copied
	size_t num_smpls = time_end - timestamp;
	if (num_smpls > len)
		num_smpls = len;

	// Starting index
	size_t read_start = data_start + (timestamp - time_start);

	// Read it
	if (read_start + num_smpls < buf_len) {
		size_t numBytes = len * type_sz;
		memcpy(buf, data + read_start, numBytes);
	} else {
		size_t first_cp = (buf_len - read_start) * type_sz;
		size_t second_cp = len * type_sz - first_cp;

		memcpy(buf, data + read_start, first_cp);
		memcpy((char*) buf + first_cp, data, second_cp);
	}

	data_start = (read_start + len) % buf_len;
	time_start = timestamp + len;

	if (time_start > time_end)
		return ERROR_READ;
	else
		return num_smpls;
}

ssize_t smpl_buf::read(void *buf, size_t len, uhd::time_spec_t ts)
{
	return read(buf, len, ts.to_ticks(clk_rt));
}

ssize_t smpl_buf::write(void *buf, size_t len, TIMESTAMP timestamp)
{
	int type_sz = 2 * sizeof(short);

	// Check for valid write
	if ((len == 0) || (len >= buf_len))
		return ERROR_WRITE;
	if ((timestamp + len) <= time_end)
		return ERROR_TIMESTAMP;

	// Starting index
	size_t write_start = (data_start + (timestamp - time_start)) % buf_len;

	// Write it
	if ((write_start + len) < buf_len) {
		size_t numBytes = len * type_sz;
		memcpy(data + write_start, buf, numBytes);
	} else {
		size_t first_cp = (buf_len - write_start) * type_sz;
		size_t second_cp = len * type_sz - first_cp;

		memcpy(data + write_start, buf, first_cp);
		memcpy(data, (char*) buf + first_cp, second_cp);
	}

	data_end = (write_start + len) % buf_len;
	time_end = timestamp + len;

	if (((write_start + len) > buf_len) && (data_end > data_start))
		return ERROR_OVERFLOW;
	else if (time_end <= time_start)
		return ERROR_WRITE;
	else
		return len;
}

ssize_t smpl_buf::write(void *buf, size_t len, uhd::time_spec_t ts)
{
	return write(buf, len, ts.to_ticks(clk_rt));
}

std::string smpl_buf::str_status() const
{
	std::ostringstream ost("Sample buffer: ");

	ost << "length = " << buf_len;
	ost << ", time_start = " << time_start;
	ost << ", time_end = " << time_end;
	ost << ", data_start = " << data_start;
	ost << ", data_end = " << data_end;

	return ost.str();
}

std::string smpl_buf::str_code(ssize_t code)
{
	switch (code) {
	case ERROR_TIMESTAMP:
		return "Sample buffer: Requested timestamp is not valid";
	case ERROR_READ:
		return "Sample buffer: Read error";
	case ERROR_WRITE:
		return "Sample buffer: Write error";
	case ERROR_OVERFLOW:
		return "Sample buffer: Overrun";
	default:
		return "Sample buffer: Unknown error";
	}
}

RadioDevice *RadioDevice::make(int sps, bool skip_rx)
{
	return new uhd_device(sps, skip_rx);
}
