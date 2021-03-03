#include <ortp/ortp.h>

int main(int argc, char **argv)
{
	RtpSession * mSession;
	mSession = rtp_session_new(RTP_SESSION_SENDRECV);
	rtp_session_set_local_addr(mSession, "0.0.0.0", -1, -1);
}
