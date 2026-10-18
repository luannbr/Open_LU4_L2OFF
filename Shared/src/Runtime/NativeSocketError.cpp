#include "Shared/src/Runtime/NativeSocketError.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#else
#include <cerrno>
#endif

namespace portable {
namespace {

#if !defined(_WIN32)
int WinSockCompatibleError(int error) noexcept {
#ifdef EINTR
    if (error == EINTR) {
        return 10004;
    }
#endif
#ifdef EBADF
    if (error == EBADF) {
        return 10009;
    }
#endif
#ifdef EACCES
    if (error == EACCES) {
        return 10013;
    }
#endif
#ifdef EFAULT
    if (error == EFAULT) {
        return 10014;
    }
#endif
#ifdef EINVAL
    if (error == EINVAL) {
        return 10022;
    }
#endif
#ifdef EMFILE
    if (error == EMFILE) {
        return 10024;
    }
#endif
#ifdef EAGAIN
    if (error == EAGAIN) {
        return 10035;
    }
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || EWOULDBLOCK != EAGAIN)
    if (error == EWOULDBLOCK) {
        return 10035;
    }
#endif
#ifdef EINPROGRESS
    if (error == EINPROGRESS) {
        return 10036;
    }
#endif
#ifdef EALREADY
    if (error == EALREADY) {
        return 10037;
    }
#endif
#ifdef ENOTSOCK
    if (error == ENOTSOCK) {
        return 10038;
    }
#endif
#ifdef EDESTADDRREQ
    if (error == EDESTADDRREQ) {
        return 10039;
    }
#endif
#ifdef EMSGSIZE
    if (error == EMSGSIZE) {
        return 10040;
    }
#endif
#ifdef EPROTOTYPE
    if (error == EPROTOTYPE) {
        return 10041;
    }
#endif
#ifdef ENOPROTOOPT
    if (error == ENOPROTOOPT) {
        return 10042;
    }
#endif
#ifdef EPROTONOSUPPORT
    if (error == EPROTONOSUPPORT) {
        return 10043;
    }
#endif
#ifdef ESOCKTNOSUPPORT
    if (error == ESOCKTNOSUPPORT) {
        return 10044;
    }
#endif
#ifdef EOPNOTSUPP
    if (error == EOPNOTSUPP) {
        return 10045;
    }
#endif
#if defined(ENOTSUP) && (!defined(EOPNOTSUPP) || ENOTSUP != EOPNOTSUPP)
    if (error == ENOTSUP) {
        return 10045;
    }
#endif
#ifdef EPFNOSUPPORT
    if (error == EPFNOSUPPORT) {
        return 10046;
    }
#endif
#ifdef EAFNOSUPPORT
    if (error == EAFNOSUPPORT) {
        return 10047;
    }
#endif
#ifdef EADDRINUSE
    if (error == EADDRINUSE) {
        return 10048;
    }
#endif
#ifdef EADDRNOTAVAIL
    if (error == EADDRNOTAVAIL) {
        return 10049;
    }
#endif
#ifdef ENETDOWN
    if (error == ENETDOWN) {
        return 10050;
    }
#endif
#ifdef ENETUNREACH
    if (error == ENETUNREACH) {
        return 10051;
    }
#endif
#ifdef ENETRESET
    if (error == ENETRESET) {
        return 10052;
    }
#endif
#ifdef ECONNABORTED
    if (error == ECONNABORTED) {
        return 10053;
    }
#endif
#ifdef ECONNRESET
    if (error == ECONNRESET) {
        return 10054;
    }
#endif
#ifdef ENOBUFS
    if (error == ENOBUFS) {
        return 10055;
    }
#endif
#ifdef EISCONN
    if (error == EISCONN) {
        return 10056;
    }
#endif
#ifdef ENOTCONN
    if (error == ENOTCONN) {
        return 10057;
    }
#endif
#ifdef ESHUTDOWN
    if (error == ESHUTDOWN) {
        return 10058;
    }
#endif
#ifdef ETOOMANYREFS
    if (error == ETOOMANYREFS) {
        return 10059;
    }
#endif
#ifdef ETIMEDOUT
    if (error == ETIMEDOUT) {
        return 10060;
    }
#endif
#ifdef ECONNREFUSED
    if (error == ECONNREFUSED) {
        return 10061;
    }
#endif
#ifdef ELOOP
    if (error == ELOOP) {
        return 10062;
    }
#endif
#ifdef ENAMETOOLONG
    if (error == ENAMETOOLONG) {
        return 10063;
    }
#endif
#ifdef EHOSTDOWN
    if (error == EHOSTDOWN) {
        return 10064;
    }
#endif
#ifdef EHOSTUNREACH
    if (error == EHOSTUNREACH) {
        return 10065;
    }
#endif
#ifdef ENOTEMPTY
    if (error == ENOTEMPTY) {
        return 10066;
    }
#endif
    return error;
}
#endif

} // namespace

int GetNativeSocketLastError() noexcept {
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return WinSockCompatibleError(errno);
#endif
}

void SetNativeSocketLastError(int error) noexcept {
#if defined(_WIN32)
    ::WSASetLastError(error);
#else
    // Synthetic portable completions keep their WinSock number unchanged.
    // the translator returns unknown values unchanged.
    errno = error;
#endif
}

} // namespace portable
