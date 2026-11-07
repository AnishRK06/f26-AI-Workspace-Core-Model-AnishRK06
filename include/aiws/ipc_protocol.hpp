#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace aiws::ipc {

// CR-1 Remote Add wire format, shared by aiws_send (tools/) and the app's IPC
// server so both sides read the same constants. No Qt, so aiws_send doesn't
// have to link the GUI.
//
// one request per connection, over a POSIX Unix domain stream socket. all
// integers are unsigned big-endian
//
//   request (aiws_send -> app)
//     byte 0       command   0x01 = add
//     bytes 1-4    N         path length in bytes, 1 <= N <= kMaxPathBytes
//     bytes 5..    path      N bytes of UTF-8, absolute, no NUL at the end
//
//   reply (app -> aiws_send)
//     byte 0       status    0x00 = accepted, 0x01 = rejected
//     bytes 1-4    M         reason length in bytes (0 when accepted)
//     bytes 5..    reason    M bytes of UTF-8, no NUL at the end
//
// the app closes the connection after the reply. the path is length-prefixed
// instead of ending at a newline or NUL, so spaces, UTF-8 and newlines need
// no escaping (E2), and the reader knows exactly how many bytes to wait for
// when they arrive in pieces (E3)

constexpr std::uint8_t kCommandAdd = 0x01;
constexpr std::uint8_t kStatusAccepted = 0x00;
constexpr std::uint8_t kStatusRejected = 0x01;

// 1 command/status byte + 4 length bytes
constexpr std::size_t kHeaderBytes = 5;

// anything longer is a broken client, not a real path
constexpr std::uint32_t kMaxPathBytes = 64 * 1024;

// aiws_send gives up reaching the app after 2 s, and the whole exchange after 5 s
constexpr int kConnectTimeoutMs = 2000;
constexpr int kExchangeTimeoutMs = 5000;

// the server drops a client that hasn't sent a full request by then, so one
// stuck client can't block the others or the shutdown
constexpr int kRequestTimeoutMs = 5000;

// longest single poll() wait. short enough that anything blocked on a socket
// sees the stop flag well inside E4's 500 ms
constexpr int kPollSliceMs = 100;

using Clock = std::chrono::steady_clock;

// /tmp/aiws-$USER.sock, or /tmp/aiws-<uid>.sock if USER isn't set
std::string default_socket_path();

// header + body in one buffer, so it can go out in one write_all
std::string encode(std::uint8_t code, const std::string& body);

// reads the 4 length bytes of a header back into a number
std::uint32_t decode_length(const unsigned char* bytes);

// reads exactly n bytes, however many pieces they come in (E3). false on EOF
// before n bytes, a socket error, the deadline, or stop being set
bool read_exact(int fd, void* buffer, std::size_t n, Clock::time_point deadline,
                const std::atomic<bool>* stop = nullptr);

// writes all n bytes. same false cases as read_exact
bool write_all(int fd, const void* buffer, std::size_t n, Clock::time_point deadline,
               const std::atomic<bool>* stop = nullptr);

}  // namespace aiws::ipc
