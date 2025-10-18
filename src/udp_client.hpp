#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <optional>
#include <vector>
#include <charconv>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using socket_t = SOCKET;
using socklen_t = int;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
using socket_t = int;
#endif

/// @brief ServerInfo — class/struct documentation.
struct ServerInfo
{
	std::string name;
	std::string ip;
	uint16_t port;
	uint64_t last_seen;
};

/// @brief UdpClient — class/struct documentation.
class UdpClient
{
public:
	UdpClient(std::uint16_t start_port, std::uint16_t end_port, std::uint64_t keepalive_ms = 1000);
	~UdpClient();
	UdpClient(const UdpClient&) = delete;
	UdpClient& operator=(const UdpClient&) = delete;


	void tick(std::vector<std::string>& out_read);
	std::vector<ServerInfo> scan();
	void start_session(const ServerInfo& s);
	void stop_session();
	std::uint32_t latency();

	bool connected() const { return connected_; }
	std::string server_endpoint() const; // "ip:port"


private:
	// ===== Config protocole / scurit simple =====
	static constexpr std::string_view kDiscoverMsg{ "DISCOVER_DEMO" };
	static constexpr std::string_view kOfferPrefix{ "OFFER" };
	static constexpr std::string_view kMagicToken{ "MAGIC{vS9zyH:2p^nQ!eF#7L}" };

	// ===== Latency =====
	std::unordered_map<std::uint32_t, std::uint64_t> _ping_sent_ms;
	std::deque<std::uint32_t> _rtt_ms;
	std::uint64_t _rtt_sum_ms;
	static constexpr std::size_t kMaxRttSamples = 64;

	// ===== Socket & dcouverte =====
	socket_t s_;
	std::uint16_t start_port_;
	std::uint16_t end_port_;
	std::uint64_t last_probe_ms_;
	std::uint16_t current_port_;
	std::vector<ServerInfo> _servers;
	std::vector<std::string> _readed;


	// ===== Session / keepalive =====
	bool connected_;
	sockaddr_in srv_addr_;
	std::uint16_t srv_port_;
	std::uint64_t keepalive_ms_;
	std::uint64_t next_ping_ms_;
	std::uint64_t last_pong_ms_;
	std::uint32_t seq_;


	// --- internes ---
	static std::uint64_t now_ms_();
	static void net_init_();
	static void net_cleanup_();
	static bool would_block_();
	static void set_nonblock_(socket_t s);
	static socket_t make_udp_(bool broadcast, bool reuse = true);
	static std::string get_ip_from_sockaddr_(const sockaddr_in& a);


	void probe_once_(std::uint16_t port);
	void read_all_();
	void send_ping_if_needed_();
	void check_timeout_();

	static std::uint16_t parse_offer_port_(std::string_view offer, std::uint16_t fallback);
	static std::string parse_offer_name_(std::string_view offer);
};
