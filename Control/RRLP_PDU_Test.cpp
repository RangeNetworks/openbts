#include "Configuration.h"
#include "RRLPQueryController.h"

using namespace GSM;
using namespace GSM::RRLP;

// compile one liner:
// g++ -DRRLP_TEST_HACK -L../GSM/.libs -L../CommonLibs/.libs -I../CLI -I../Globals -I../GSM -I../CommonLibs RRLPQueryController.cpp RRLP_PDU_Test.cpp -lcommon -lGSM -lpthread -o RRLP_PDU_Test

// Required by various stuff - I think a TODO is to allow tests to work without
// having to copy this line around.
ConfigurationTable gConfig("OpenBTS.config");

int main()
{
    // This is a MsrPositionRsp with valid coordinates
    // 38.28411340713501,237.95414686203003,22 (as parsed by the erlang automatically generated
    // implementation - checked against a map).
    BitVector test("0000011000111000000000000001011000100010000100011111111111111111010010101111101111011110101101100100000011010110110100111000111010100011110010010100111000000000010001000001100000011000110010000010000100010000");
    RRLPQueryController c(test);
    return 0;
}

