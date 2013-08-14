#include <iostream>

#include <GSML3RRMessages.h>
#include <GSMTransfer.h>

// Load configuration from a file.
ConfigurationTable gConfig("OpenBTS.config");

int main()
{
    GSM::L3ApplicationInformation ai();
    static const char init_request_msbased_gps[4] = {'@', '\x01', 'x', '\xa8'}; // pre encoded PER for the following XER:
    static std::vector<char> request_msbased_gps(init_request_msbased_gps,
        init_request_msbased_gps + sizeof(init_request_msbased_gps));
    GSM::L3ApplicationInformation ai2(request_msbased_gps);

    GSM::L3Frame f(ai2);
    std::cout << f;

    return 0;
}

