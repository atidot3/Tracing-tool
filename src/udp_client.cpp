#include "udp_client.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>

// ===== Implémentations des fonctions internes manquantes =====
std::uint64_t UdpClient::now_ms_()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void UdpClient::net_init_() {
#ifdef _WIN32
    static bool inited = false;
    if (!inited) {
        WSADATA w{};
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0)
        {
            std::cerr << "WSAStartup failed";
            std::exit(1);}
            inited = true;
        }
#endif
}

void UdpClient::net_cleanup_() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool UdpClient::would_block_() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void UdpClient::set_nonblock_(socket_t s) {
#ifdef _WIN32
    u_long m = 1; ioctlsocket(s, FIONBIO, &m);
#else
    int fl = fcntl(s, F_GETFL, 0); if (fl < 0) fl = 0; fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

socket_t UdpClient::make_udp_(bool broadcast, bool reuse) {
#ifdef _WIN32
    socket_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) { std::perror("socket"); std::exit(1); }
#else
    socket_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { std::perror("socket"); std::exit(1); }
#endif
    if (reuse) {
        int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
#ifdef SO_REUSEPORT
        setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (char*)&yes, sizeof(yes));
#endif
    }
    if (broadcast)
    {
        int yes = 1;
        if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes)) < 0)
        {
            std::perror("setsockopt(SO_BROADCAST)");
            std::exit(1);
        }
    }
    set_nonblock_(s);
    return s;
}

std::string UdpClient::get_ip_from_sockaddr_(const sockaddr_in & a)
{
    char ip[64]{};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return std::string(ip[0] ? ip : "?");
}

// ===== Ctor / Dtor =====
UdpClient::UdpClient(std::uint16_t start_port, std::uint16_t end_port, std::uint64_t keepalive_ms)
    : start_port_(start_port)
    , end_port_(end_port)
    , current_port_(start_port)
    , keepalive_ms_(keepalive_ms)
{
    net_init_();
    s_ = make_udp_(/*broadcast*/true, /*reuse*/true);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_, (sockaddr*)&local, sizeof(local)) < 0)
    {
        std::perror("bind(client)");
        std::exit(1);
    }
    next_probe_ms_ = now_ms_();
}

UdpClient::~UdpClient() {
#ifdef _WIN32
    if (s_ != INVALID_SOCKET) closesocket(s_);
#else
    if (s_ >= 0) ::close(s_);
#endif
    net_cleanup_();
}

std::string UdpClient::server_endpoint() const
{
    return connected_ ? get_ip_from_sockaddr_(srv_addr_) + ":" + std::to_string(srv_port_) : std::string{};
}

// ===== Découverte =====
std::vector<ServerInfo> UdpClient::scan()
{
    const auto t = now_ms_();
    if (!connected_ && t >= next_probe_ms_)
    {
        for (std::uint16_t p = start_port_; p <= end_port_; ++p)
            probe_once_(p);

        next_probe_ms_ = t + 1000;
        std::erase_if(_servers, [t](const ServerInfo& info)
        {
            return t - info.last_seen > 5000;
        });
    }
    return _servers;
}

void UdpClient::probe_once_(std::uint16_t port)
{
    std::string discover = std::string(kDiscoverMsg) + " token=" + std::string(kMagicToken);
    sockaddr_in b{};
    b.sin_family = AF_INET;
    b.sin_port = htons(port);
    b.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    int r = sendto(s_, discover.c_str(), (int)discover.size(), 0, (sockaddr*)&b, sizeof(b));
    if (r < 0 && !would_block_())
        std::perror("sendto(broadcast)");
}

std::uint16_t UdpClient::parse_offer_port_(std::string_view offer, std::uint16_t fallback)
{
    auto i = offer.find("port=");
    if (i == std::string_view::npos) return fallback;
    i += 5;
    while (i < offer.size() && (offer[i] == ' ' || offer[i] == '\t')) ++i;

    auto j = i;
    while (j < offer.size() && offer[j] >= '0' && offer[j] <= '9') ++j;

    unsigned v = 0;
    auto res = std::from_chars(offer.data() + i, offer.data() + j, v, 10);
    if (res.ec != std::errc{}) return fallback;
    if (v > 65535u) v = 65535u;
    return static_cast<std::uint16_t>(v);
}

std::string UdpClient::parse_offer_name_(std::string_view offer)
{
    auto i = offer.find("name=");
    if (i == std::string_view::npos)
        return "udp_server";

    i += 5;
    while (i < offer.size() && (offer[i] == ' ' || offer[i] == '\t'))
        ++i;
    if (i >= offer.size())
        return "udp_server";

    if (offer[i] == '"' || offer[i] == '\'')
    {
        const char quote = offer[i++];
        auto j = offer.find(quote, i);
        if (j == std::string_view::npos)
            j = offer.find_first_of("\r\n", i);
        auto sv = offer.substr(i, (j == std::string_view::npos ? offer.size() - i : j - i));
        return std::string(sv);
    }

    auto j = offer.find_first_of(" \t\r\n", i);
    auto sv = offer.substr(i, (j == std::string_view::npos ? offer.size() - i : j - i));
    return std::string(sv);
}

void UdpClient::start_session(const ServerInfo& s)
{
    sockaddr_in b{};
    b.sin_family = AF_INET;
    b.sin_port = htons(s.port);

#ifdef _WIN32
    // nécessite <ws2tcpip.h>
    if (InetPtonA(AF_INET, s.ip.c_str(), &b.sin_addr) != 1) {
        // IP invalide
        connected_ = false;
        return;
    }
#else
    if (inet_pton(AF_INET, s.ip.c_str(), &b.sin_addr) != 1) {
        // IP invalide
        connected_ = false;
        return;
    }
#endif
    connected_ = true;
    srv_addr_ = b;
    srv_port_ = s.port;

    last_pong_ms_ = now_ms_();
    next_ping_ms_ = last_pong_ms_;
    seq_ = 0;
}

void UdpClient::stop_session()
{
    connected_ = false;
    _servers.clear();
}

// ===== Session =====
void UdpClient::send_ping_if_needed_()
{
    if (!connected_)
        return;
    const auto t = now_ms_();
    if (t < next_ping_ms_)
        return;
    char msg[64];
    std::snprintf(msg, sizeof(msg), "PING %u", ++seq_);
    sockaddr_in dst = srv_addr_;
    dst.sin_port = htons(srv_port_);
    int r = sendto(s_, msg, (int)std::strlen(msg), 0, (sockaddr*)&dst, sizeof(dst));
    if (r < 0 && !would_block_())
        std::perror("sendto(PING)");
    next_ping_ms_ = t + keepalive_ms_;
}

void UdpClient::read_all_()
{
    for (;;)
    {
        char buf[2048]; sockaddr_in from{}; socklen_t fl = sizeof(from);
        int read_size = recvfrom(s_, buf, (int)sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (read_size < 0)
        {
            if (would_block_())
                break;
            std::perror("recvfrom(client)");
            break;
        }
        buf[read_size] = '\0';

        if (!connected_ && static_cast<std::size_t>(read_size) >= kOfferPrefix.size() && std::string_view(buf, kOfferPrefix.size()) == kOfferPrefix)
        {
            auto offer_port = parse_offer_port_(buf, 0);
            auto server_name = parse_offer_name_(buf);
            auto ip = get_ip_from_sockaddr_(from);
            std::string key = ip + ":" + std::to_string(offer_port);

            for (auto& s : _servers)
            {
                if (s.name == server_name && s.port == offer_port && s.ip == ip)
                {
                    s.last_seen = now_ms_();
                    continue;
                }
            }
            _servers.push_back(ServerInfo{ server_name, ip, offer_port });
            continue;
        }

        if (read_size >= 4 && std::string_view(buf, 4) == std::string_view("PONG", 4))
        {
            last_pong_ms_ = now_ms_();
        }
        else if (read_size >= 10 && std::string_view(buf, 10) == std::string_view("SERVER_MSG", 10))
        {
            std::cout << "Server message: " << buf << std::endl;
        }
        else
        {
            _readed.push_back(std::string(buf, read_size));
        }
    }
}

void UdpClient::check_timeout_()
{
    if (connected_ && now_ms_() - last_pong_ms_ > 3 * keepalive_ms_)
        stop_session();
}

void UdpClient::tick(std::vector<std::string>& out_read)
{
    read_all_();
    send_ping_if_needed_();
    check_timeout_();
    out_read = std::move(_readed);
}
