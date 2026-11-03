#include "Uart.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace
{
int RemainingMs(const std::chrono::steady_clock::time_point& deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }

    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return remaining.count() > 0 ? static_cast<int>(remaining.count()) : 1;
}
} // namespace

Uart::Uart(const std::string& device, int baud)
{
    Open(device, baud);
}

Uart::~Uart()
{
    Close();
}

bool Uart::Open(const std::string& device, int baud)
{
    Close();

    fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) {
        std::fprintf(stderr, "open serial port '%s' failed: %s\n",
                     device.c_str(), std::strerror(errno));
        return false;
    }

    if (!Configure(baud)) {
        std::fprintf(stderr, "configure serial port '%s' failed\n",
                     device.c_str());
        Close();
        return false;
    }

    std::printf("serial opened: %s, %d 8N1\n", device.c_str(), baud);
    return true;
}

bool Uart::InitSerialPort(const std::string& device, int baud)
{
    return Open(device, baud);
}

void Uart::Close()
{
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool Uart::IsOpen() const
{
    return fd_ >= 0;
}

bool Uart::BaudToTermios(int baud, unsigned long& speed)
{
    switch (baud) {
    case 9600: speed = B9600; return true;
    case 19200: speed = B19200; return true;
    case 38400: speed = B38400; return true;
    case 57600: speed = B57600; return true;
    case 115200: speed = B115200; return true;
    case 230400: speed = B230400; return true;
    case 460800: speed = B460800; return true;
    case 921600: speed = B921600; return true;
    case 1000000: speed = B1000000; return true;
    default: return false;
    }
}

bool Uart::Configure(int baud)
{
    unsigned long speed_value = 0;
    if (!BaudToTermios(baud, speed_value)) {
        std::fprintf(stderr, "unsupported baud rate: %d\n", baud);
        return false;
    }

    termios options{};
    if (tcgetattr(fd_, &options) != 0) {
        std::fprintf(stderr, "tcgetattr failed: %s\n", std::strerror(errno));
        return false;
    }

    cfmakeraw(&options);
    if (cfsetispeed(&options, static_cast<speed_t>(speed_value)) != 0 ||
        cfsetospeed(&options, static_cast<speed_t>(speed_value)) != 0) {
        std::fprintf(stderr, "set baud rate failed: %s\n", std::strerror(errno));
        return false;
    }

    options.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    options.c_cflag |= CS8 | CLOCAL | CREAD;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    if (tcflush(fd_, TCIOFLUSH) != 0) {
        std::fprintf(stderr, "tcflush failed: %s\n", std::strerror(errno));
        return false;
    }
    if (tcsetattr(fd_, TCSANOW, &options) != 0) {
        std::fprintf(stderr, "tcsetattr failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

int Uart::Read(std::uint8_t* data, std::size_t size, int timeout_ms)
{
    if (!IsOpen() || data == nullptr || size == 0) {
        errno = EINVAL;
        return -1;
    }

    pollfd descriptor{fd_, POLLIN, 0};
    int poll_result;
    do {
        poll_result = poll(&descriptor, 1, timeout_ms);
    } while (poll_result < 0 && errno == EINTR);

    if (poll_result == 0) {
        return 0;
    }
    if (poll_result < 0 ||
        (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return -1;
    }

    ssize_t count;
    do {
        count = read(fd_, data, size);
    } while (count < 0 && errno == EINTR);

    if (count < 0 && errno == EAGAIN) {
        return 0;
    }
    return count < 0 ? -1 : static_cast<int>(count);
}

int Uart::Write(const std::uint8_t* data, std::size_t size)
{
    if (!IsOpen() || data == nullptr || size == 0) {
        errno = EINVAL;
        return -1;
    }

    ssize_t count;
    do {
        count = write(fd_, data, size);
    } while (count < 0 && errno == EINTR);
    return count < 0 ? -1 : static_cast<int>(count);
}

bool Uart::ReadExact(std::uint8_t* data, std::size_t size, int timeout_ms)
{
    if (!IsOpen() || data == nullptr || size == 0 || timeout_ms < 0) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    std::size_t received = 0;

    while (received < size) {
        const int remaining_ms = RemainingMs(deadline);
        if (remaining_ms <= 0) {
            return false;
        }

        const int count = Read(data + received, size - received, remaining_ms);
        if (count < 0) {
            return false;
        }
        if (count == 0) {
            continue;
        }
        received += static_cast<std::size_t>(count);
    }
    return true;
}

bool Uart::WriteAll(const std::uint8_t* data, std::size_t size)
{
    if (!IsOpen() || data == nullptr || size == 0) {
        return false;
    }

    std::size_t written = 0;
    while (written < size) {
        const int count = Write(data + written, size - written);
        if (count <= 0) {
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
}

bool Uart::FlushInput()
{
    return IsOpen() && tcflush(fd_, TCIFLUSH) == 0;
}
