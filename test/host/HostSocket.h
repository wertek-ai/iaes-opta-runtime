/**
 * A TCP socket, on a PC, wearing the shape Arduino's Client has.
 *
 * The point of these headers is that the runtime compiles and runs unmodified
 * on a laptop, against a software Modbus slave and a real broker. Without that
 * nobody can try this library without buying an Opta -- and a reference
 * implementation nobody can run teaches nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HOST_SOCKET_H
#define HOST_SOCKET_H

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET host_fd_t;
  #define HOST_INVALID INVALID_SOCKET
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <unistd.h>
  typedef int host_fd_t;
  #define HOST_INVALID (-1)
#endif

namespace hostnet {

inline void startup() {
#ifdef _WIN32
    static bool done = false;
    if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = true; }
#endif
}

class Socket {
public:
    Socket() : _fd(HOST_INVALID) {}
    ~Socket() { close(); }

    bool connect(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms) {
        startup();
        close();
        _fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_fd == HOST_INVALID) return false;

        int one = 1;
        setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
        setTimeout(timeout_ms);

        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = ipv4_be;
        if (::connect(_fd, (sockaddr*)&a, sizeof(a)) != 0) { close(); return false; }
        return true;
    }

    void setTimeout(uint32_t ms) {
        if (_fd == HOST_INVALID) return;
#ifdef _WIN32
        DWORD t = ms;
        setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
        setsockopt(_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
#else
        timeval t{(time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000)};
        setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
        setsockopt(_fd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
#endif
    }

    bool connected() const { return _fd != HOST_INVALID; }

    void close() {
        if (_fd == HOST_INVALID) return;
#ifdef _WIN32
        closesocket(_fd);
#else
        ::close(_fd);
#endif
        _fd = HOST_INVALID;
    }

    bool writeAll(const uint8_t* data, size_t len) {
        while (len) {
            int n = ::send(_fd, (const char*)data, (int)len, 0);
            if (n <= 0) return false;
            data += n; len -= n;
        }
        return true;
    }

    /** Reads exactly len bytes, or fails. A partial frame is not a frame. */
    bool readExactly(uint8_t* out, size_t len) {
        while (len) {
            int n = ::recv(_fd, (char*)out, (int)len, 0);
            if (n <= 0) return false;
            out += n; len -= n;
        }
        return true;
    }

private:
    host_fd_t _fd;
};

}  // namespace hostnet

#endif  // HOST_SOCKET_H
