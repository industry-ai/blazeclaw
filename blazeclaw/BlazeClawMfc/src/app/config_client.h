#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <iostream>

class ConfigClient {
public:
    // Meyers-style singleton
    static ConfigClient& instance() {
        static ConfigClient inst;
        return inst;
    }

    // Load configuration from given path. If file missing, keep previous values.
    void loadFromPath(const std::string& path) {
        std::ifstream in(path);
        if (!in) {
            std::lock_guard<std::mutex> lk(m_);
            std::cerr << "[WARN] Client config file '" << path << "' not found; using previous or defaults"
                      << std::endl;
            return;
        }

        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

        Config tmp = getCopy();

        // parse simple key=value lines first
        std::istringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            auto start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            if (line[start] == '#') continue;
            auto eq = line.find('=', start);
            if (eq == std::string::npos) continue;
            std::string key = line.substr(start, eq - start);
            auto endk = key.find_last_not_of(" \t");
            if (endk != std::string::npos) key = key.substr(0, endk + 1);
            std::string val = line.substr(eq + 1);
            auto b = val.find_first_not_of(" \t"); if (b != std::string::npos) val = val.substr(b);
            auto e = val.find_last_not_of(" \t\r\n"); if (e != std::string::npos) val = val.substr(0, e + 1);

            if (key == "listen_host" || key == "listen_address" || key == "host" ||
                key == "server_host" || key == "remote_server_host") {
                tmp.host = val;
            }
            else if (key == "listen_port" || key == "port" || key == "server_port" ||
                     key == "remote_server_port") {
                try { tmp.port = std::stoi(val); } catch (...) {}
            }
            else if (key == "tls_listen_host" || key == "tls_host" || key == "tls_server_host") {
                tmp.tls_host = val;
            }
            else if (key == "tls_listen_port" || key == "tls_port" || key == "tls_server_port") {
                try { tmp.tls_port = std::stoi(val); } catch (...) {}
            }
            else if (key == "login_email" || key == "email") {
                tmp.login_email = val;
            }
            else if (key == "login_password" || key == "password" || key == "passwd") {
                tmp.login_password = val;
            }
            else if (key == "test_clients" || key == "clients" || key == "remote_test_clients") {
                try { tmp.clients = std::stoi(val); } catch (...) {}
            }
            else if (key == "test_iterations" || key == "iterations" || key == "remote_test_iterations") {
                try { tmp.iterations = std::stoi(val); } catch (...) {}
            }
            else if (key == "SUPABASE_URL" || key == "supabase_url" || key == "url") {
                tmp.supabase_url = val;
            }
            else if (key == "SUPABASE_API_KEY" || key == "supabase_api_key" || key == "api_key" || key == "anon_key") {
                tmp.supabase_api_key = val;
            }
        }

        // robust extraction for JSON/TOML/YAML-like lines
        int v;
        if (extract_int_value(text, "listen_port", v)) tmp.port = v;
        if (extract_int_value(text, "port", v)) tmp.port = v;
        if (extract_int_value(text, "server_port", v)) tmp.port = v;
        if (extract_int_value(text, "remote_server_port", v)) tmp.port = v;

        if (extract_int_value(text, "TLS_listen_port", v)) tmp.tls_port = v;
        if (extract_int_value(text, "tls_listen_port", v)) tmp.tls_port = v;
        if (extract_int_value(text, "tls_port", v)) tmp.tls_port = v;
        if (extract_int_value(text, "tls_server_port", v)) tmp.tls_port = v;

        if (extract_int_value(text, "test_clients", v)) tmp.clients = v;
        if (extract_int_value(text, "clients", v)) tmp.clients = v;
        if (extract_int_value(text, "remote_test_clients", v)) tmp.clients = v;
        if (extract_int_value(text, "test_iterations", v)) tmp.iterations = v;
        if (extract_int_value(text, "iterations", v)) tmp.iterations = v;
        if (extract_int_value(text, "remote_test_iterations", v)) tmp.iterations = v;

        // host extraction for simple JSON-like lines
        {
            std::istringstream ss2(text);
            while (std::getline(ss2, line)) {
                size_t s = line.find_first_not_of(" \t\r\n");
                if (s == std::string::npos) continue;
                if (line[s] == '#') continue;
                size_t colon = line.find(':', s);
                size_t eq = line.find('=', s);
                if (colon == std::string::npos && eq == std::string::npos) continue;
                size_t sep = (colon != std::string::npos) ? colon : eq;
                std::string key = line.substr(s, sep - s);
                auto trim = [](std::string& vv) {
                    size_t a = vv.find_first_not_of(" \t\r\n\"'");
                    size_t b = vv.find_last_not_of(" \t\r\n\"'");
                    if (a == std::string::npos) { vv.clear(); return; }
                    vv = vv.substr(a, b - a + 1);
                };
                trim(key);
                std::string val = line.substr(sep + 1);
                trim(val);
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);

                if (key == "listen_host" || key == "listen_address" || key == "host" ||
                    key == "server_host" || key == "remote_server_host") {
                    tmp.host = val;
                }
                else if (key == "tls_listen_host" || key == "tls_host" || key == "tls_server_host") {
                    tmp.tls_host = val;
                }
                else if (key == "login_email" || key == "email") {
                    tmp.login_email = val;
                }
                else if (key == "login_password" || key == "password" || key == "passwd") {
                    tmp.login_password = val;
                }
                else if (key == "test_clients" || key == "clients" || key == "remote_test_clients") {
                    try { tmp.clients = std::stoi(val); } catch (...) {}
                }
                else if (key == "test_iterations" || key == "iterations" || key == "remote_test_iterations") {
                    try { tmp.iterations = std::stoi(val); } catch (...) {}
                }
                else if (key == "supabase_url" || key == "url") {
                    tmp.supabase_url = val;
                }
                else if (key == "supabase_api_key" || key == "api_key" || key == "anon_key") {
                    tmp.supabase_api_key = val;
                }
            }
        }

        tmp.validate();
        {
            std::lock_guard<std::mutex> lk(m_);
            cfg_ = tmp;
        }
        std::lock_guard<std::mutex> lk2(m_);
        std::cerr << "[INFO] Client config reloaded: host=" << cfg_.host
                  << " port=" << cfg_.port
                  << " tls_host=" << cfg_.tls_host
                  << " tls_port=" << cfg_.tls_port
                  << " clients=" << cfg_.clients
                  << " iterations=" << cfg_.iterations
                  << " supabase_url=" << (cfg_.supabase_url.empty() ? "(not set)" : cfg_.supabase_url)
                  << " supabase_api_key=" << (cfg_.supabase_api_key.empty() ? "(not set)" : "[REDACTED]") << std::endl;
    }

    // Thread-safe getters
    std::string getHost() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.host;
    }
    int getPort() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.port;
    }
    std::string getTlsHost() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.tls_host;
    }
    int getTlsPort() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.tls_port;
    }
    std::string getLoginEmail() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.login_email;
    }
    std::string getLoginPassword() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.login_password;
    }
    int getClients() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.clients;
    }
    int getIters() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.iterations;
    }
    std::string getSupabaseUrl() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.supabase_url;
    }
    std::string getSupabaseApiKey() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_.supabase_api_key;
    }

    // Allow programmatic override for tests
    void setHost(const std::string& h) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.host = h;
    }
    void setPort(int p) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.port = std::clamp(p, 1, 65535);
    }
    void setTlsHost(const std::string& h) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.tls_host = h;
    }
    void setTlsPort(int p) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.tls_port = std::clamp(p, 1, 65535);
    }
    void setLoginEmail(const std::string& email) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.login_email = email;
    }
    void setLoginPassword(const std::string& password) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.login_password = password;
    }
    void setClients(int c) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.clients = std::clamp(c, 1, 1000);
    }
    void setIters(int i) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.iterations = std::clamp(i, 1, 1000);
    }
    void setSupabaseUrl(const std::string& url) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.supabase_url = url;
    }
    void setSupabaseApiKey(const std::string& key) {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.supabase_api_key = key;
    }

private:
    struct Config {
        std::string host;
        int port = 8765;

        std::string tls_host;
        int tls_port = 9443;

        std::string login_email = "test@gmail.com";
        std::string login_password;

        std::string supabase_url;
        std::string supabase_api_key;

        int clients = 0;
        int iterations = 0;

        void validate() {
            if (host.size() == 0) host = "127.0.0.1";
            port = std::clamp(port, 1, 65535);

            if (tls_host.size() == 0) tls_host = host;
            tls_port = std::clamp(tls_port, 1, 65535);

            if (login_email.size() == 0) login_email = "test@gmail.com";

            clients = std::clamp(clients, 0, 1000);
            iterations = std::clamp(iterations, 0, 1000);
        }
    };

    ConfigClient() {
        // default constructed
    }
    ConfigClient(const ConfigClient&) = delete;
    ConfigClient& operator=(const ConfigClient&) = delete;

    Config getCopy() {
        std::lock_guard<std::mutex> lk(m_);
        return cfg_;
    }

    // helper extraction for JSON/TOML/YAML/key=value numeric tokens
    static bool extract_int_value(const std::string& text, const std::string& key, int& out) {
        size_t pos = 0;
        while (pos < text.size()) {
            pos = text.find(key, pos);
            if (pos == std::string::npos) break;
            size_t p = pos + key.size();
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
            if (p < text.size() && (text[p] == ':' || text[p] == '=' || text[p] == '"')) {
                size_t q = p;
                while (q < text.size() && !((text[q] >= '0' && text[q] <= '9') ||
                                            text[q] == '-')) ++q;
                if (q == text.size()) { pos = p; continue; }
                size_t end = q + 1;
                while (end < text.size() && (text[end] >= '0' && text[end] <= '9')) ++end;
                std::string token = text.substr(q, end - q);
                try { out = std::stoi(token); return true; }
                catch (...) { return false; }
            }
            pos = p;
        }
        return false;
    }

    Config cfg_;
    std::mutex m_;
};