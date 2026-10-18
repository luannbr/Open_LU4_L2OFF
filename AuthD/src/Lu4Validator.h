#pragma once

// Credentials of the LU4 (Liberta Ultimata 4) client. Portable addition, not
// part of the original AuthD; enabled by Lu4Mode in etc/config.txt.
//
// The LU4 login packet carries account and password under an RSA key this
// server does not have. lu4authhook.dll, injected into the client, reads them
// from the login form and sends "AUTH\t<account>\t<password>\n" to this
// validator before the client connects; the reply "OK\n" lets the client go
// on, "FAIL\n" makes it show "wrong account or password". A validated pair is
// kept for its address for five minutes and given once to the login packet
// that AuthGateD forwards from the same address. AuthGateD marks LU4 logins
// (accepted on its lu4Port) with the account "\x7fLU4".

#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

namespace lu4 {

void StartValidator(int port);

bool TakeCredentials(in_addr address, std::string& account, std::string& password);

} // namespace lu4
