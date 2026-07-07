#ifndef WIFI_CONF_HPP
#define WIFI_CONF_HPP

// Network credentials
#define WIFI_SSID "serra"
#define WIFI_PASSWORD "917083555"

// Python server address — set these to your PC's IP on the same WiFi network.
// Run bma180_server.py first; it will print the correct IP on startup.
#define SERVER_IP_1  172
#define SERVER_IP_2  20
#define SERVER_IP_3  10
#define SERVER_IP_4  2

// UDP port the Python server listens on (must match UDP_PORT in bma180_server.py)
#define SERVER_UDP_PORT  5000

// Timeouts (milliseconds)
#define WIFI_CONNECT_TIMEOUT 10000

#endif /* WIFI_CONF_HPP */
