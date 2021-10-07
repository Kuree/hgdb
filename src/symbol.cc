#include "symbol.hh"

#include <atomic>
#include <mutex>

#include "asio.hpp"
#include "db.hh"
#include "log.hh"
#include "util.hh"
#include "websocketpp/client.hpp"
#include "websocketpp/config/asio_no_tls_client.hpp"

auto constexpr TCP_SCHEMA = "tcp://";
auto constexpr WS_SCHEMA = "ws://";

// abstract out ws and tcp connections
class NetworkProvider {
public:
    virtual void send(const std::string &msg) = 0;
    virtual std::string receive() = 0;

    virtual ~NetworkProvider() = default;

    bool has_error = false;
};

class TCPNetworkProvider : public NetworkProvider {
public:
    TCPNetworkProvider(const std::string &hostname, uint16_t port) {
        io_context_ = std::make_unique<asio::io_context>();
        s_ = std::make_unique<asio::ip::tcp::socket>(*io_context_);
        asio::ip::tcp::resolver resolver(*io_context_);
        try {
            asio::connect(*s_, resolver.resolve(hostname, std::to_string(port)));
        } catch (asio::system_error &e) {
            has_error = true;
        }
    }
    void send(const std::string &msg) override { asio::write(*s_, asio::buffer(msg)); }

    std::string receive() override {
        asio::mutable_buffer buff;
        asio::read(*s_, buff);
        // assume it's well-formed
        // and in ascii
        std::string_view data(reinterpret_cast<char *>(buff.data()), buff.size());
        return std::string(data);
    }

    ~TCPNetworkProvider() override = default;

private:
    std::unique_ptr<asio::io_context> io_context_;
    std::unique_ptr<asio::ip::tcp::socket> s_;
};

class WSNetworkProvider : public NetworkProvider {
public:
    // single thread model
    explicit WSNetworkProvider(const std::string &uri) {
        client_ = std::make_unique<Client>();
        client_->init_asio();

        auto on_message = [this](const websocketpp::connection_hdl &, const MessagePtr &msg) {
            payload_ = msg->get_payload();
            has_message_ = true;
        };

        client_->set_message_handler(on_message);

        auto on_open = [this](const websocketpp::connection_hdl &handle) {
            connection_handle_ = handle;
        };

        client_->set_open_handler(on_open);

        websocketpp::lib::error_code ec;
        Client::connection_ptr con = client_->get_connection(uri, ec);
        if (ec) {
            has_error = true;
            return;
        }

        client_->connect(con);

        // run the client in the background
        bg_client_thread_ = std::thread([this]() { client_->run(); });
    }

    void send(const std::string &msg) override {
        std::lock_guard guard(lock);
        websocketpp::lib::error_code ec;
        if (client_) [[likely]] {
            client_->send(connection_handle_, msg, websocketpp::frame::opcode::text, ec);
        }
    }

    std::string receive() override {
        std::lock_guard guard(lock);
        bool v = true;
        // spin lock on has_message
        while (has_message_.compare_exchange_strong(v, false))
            ;
        return payload_;
    }

    ~WSNetworkProvider() override = default;

private:
    using Client = websocketpp::client<websocketpp::config::asio_client>;
    using MessagePtr = websocketpp::config::asio_client::message_type::ptr;
    std::unique_ptr<Client> client_;

    // synchronization using a spin lock
    std::mutex lock;
    std::atomic<bool> has_message_ = false;
    std::string payload_;

    std::thread bg_client_thread_;
    websocketpp::connection_hdl connection_handle_;
};

namespace hgdb {

class NetworkSymbolTableProvider : public SymbolTableProvider {
public:
    explicit NetworkSymbolTableProvider(std::unique_ptr<NetworkProvider> &&network)
        : network_(std::move(network)) {}

    // helper functions to query the database
    std::vector<BreakPoint> get_breakpoints(const std::string &filename,
                                            uint32_t line_num) override {
        return get_breakpoints(filename, line_num, 0);
    }
    std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                            uint32_t col_num) override {}
    std::vector<BreakPoint> get_breakpoints(const std::string &filename) override {}
    std::optional<BreakPoint> get_breakpoint(uint32_t breakpoint_id) override {}
    std::optional<std::string> get_instance_name_from_bp(uint32_t breakpoint_id) override {}
    std::optional<std::string> get_instance_name(uint32_t id) override {}
    std::optional<uint64_t> get_instance_id(const std::string &instance_name) override {}
    [[nodiscard]] std::optional<uint64_t> get_instance_id(uint64_t breakpoint_id) override {}
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id) override {
        return get_context_variables(breakpoint_id, true);
    }
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id, bool resolve_hierarchy_value) override {}
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) override {
        return get_generator_variable(instance_id, true);
    }
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id, bool resolve_hierarchy_value) override {}
    [[nodiscard]] std::vector<std::string> get_instance_names() override {}
    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name) override {

    }
    std::unordered_map<std::string, int64_t> get_context_static_values(
        uint32_t breakpoint_id) override {}
    std::vector<std::string> get_all_signal_names() override {}

    // resolve filename or symbol names
    void set_src_mapping(const std::map<std::string, std::string> &mapping) override {}
    [[nodiscard]] std::string resolve_filename_to_db(const std::string &filename) const override {}
    [[nodiscard]] std::string resolve_filename_to_client(
        const std::string &filename) const override {}
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_breakpoint(
        const std::string &scoped_name, uint64_t breakpoint_id) override {}
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_instance(
        const std::string &scoped_name, uint64_t instance_id) override {}

    // accessors
    [[nodiscard]] const std::vector<uint32_t> &execution_bp_orders() const override {
        // will be cached to avoid network round trip
        return execution_bp_orders_;
    }

    ~NetworkSymbolTableProvider() override = default;

private:
    std::unique_ptr<NetworkProvider> network_;

    std::vector<uint32_t> execution_bp_orders_;
};

std::unique_ptr<SymbolTableProvider> create_symbol_table(const std::string &filename) {
    // we use some simple way to tell which schema it is
    if (filename.starts_with(TCP_SCHEMA)) {
        auto tokens = util::get_tokens(filename, ":");
        if (tokens.size() != 3) {
            log::log(log::log_level::error, "Invalid TCP URI " + filename);
            return nullptr;
        }
        auto port = util::stoul(tokens.back());
        if (!port) {
            log::log(log::log_level::error, "Invalid TCP port number " + tokens.back());
            return nullptr;
        }
        auto hostname = tokens[1];
        auto tcp = std::make_unique<TCPNetworkProvider>(hostname, *port);
        if (tcp->has_error) {
            log::log(log::log_level::error, "Invalid TCP UTI " + filename);
            return nullptr;
        }
        return std::make_unique<NetworkSymbolTableProvider>(std::move(tcp));
    } else if (filename.starts_with(WS_SCHEMA)) {
        auto ws = std::make_unique<WSNetworkProvider>(filename);
        if (ws->has_error) {
            log::log(log::log_level::error, "Invalid websocket UTI " + filename);
            return nullptr;
        }
        return std::make_unique<NetworkSymbolTableProvider>(std::move(ws));
    } else {
        return std::make_unique<DBSymbolTableProvider>(filename);
    }
}
}  // namespace hgdb
