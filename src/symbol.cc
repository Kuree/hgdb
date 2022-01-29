#include "symbol.hh"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "asio.hpp"
#include "db.hh"
#include "log.hh"
#include "proto.hh"
#include "thread.hh"
#include "util.hh"
#include "websocketpp/client.hpp"
#include "websocketpp/config/asio_no_tls_client.hpp"

auto constexpr TCP_SCHEMA = "tcp://";
auto constexpr WS_SCHEMA = "ws://";

namespace hgdb {

std::string resolve(const std::string &src_path, const std::string &dst_path,
                    const std::string &target) {
    namespace fs = std::filesystem;
    if (target.starts_with(src_path)) [[likely]] {
        std::error_code ec;
        auto path = fs::relative(target, src_path, ec);
        if (ec.value()) [[unlikely]]
            return target;
        fs::path start = dst_path;
        auto r = start / path;
        return r;
    } else {
        return target;
    }
}

void SymbolTableProvider::set_src_mapping(const std::map<std::string, std::string> &mapping) {
    src_remap_ = mapping;
}

std::string SymbolTableProvider::resolve_filename_to_db(const std::string &filename) {
    namespace fs = std::filesystem;
    // optimize for local use case
    if (src_remap_.empty()) [[likely]]
        return filename;
    for (auto const &[src_path, dst_path] : src_remap_) {
        if (filename.starts_with(src_path)) {
            return resolve(src_path, dst_path, filename);
        }
    }
    return filename;
}

std::string SymbolTableProvider::resolve_filename_to_client(const std::string &filename) {
    namespace fs = std::filesystem;
    // optimize for local use case
    if (src_remap_.empty()) [[likely]]
        return filename;
    for (auto const &[dst_path, src_path] : src_remap_) {
        if (filename.starts_with(src_path)) {
            return resolve(src_path, dst_path, filename);
        }
    }
    return filename;
}

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
            auto endpoint = resolver.resolve(hostname, std::to_string(port))->endpoint();
            s_->connect(endpoint);
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
        client_->clear_access_channels(websocketpp::log::alevel::all);
        client_->init_asio();

        auto on_message = [this](const websocketpp::connection_hdl &, const MessagePtr &msg) {
            payload_ = msg->get_payload();
            has_message_.ready();
        };

        client_->set_message_handler(on_message);

        RuntimeLock l;

        auto on_open = [this, &l](const websocketpp::connection_hdl &handle) {
            connection_handle_ = handle;
            l.ready();
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

        // block until it's properly connected
        l.wait();
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
        has_message_.wait();
        return payload_;
    }

    ~WSNetworkProvider() override {
        client_->stop();
        bg_client_thread_.join();
    }

private:
    using Client = websocketpp::client<websocketpp::config::asio_client>;
    using MessagePtr = websocketpp::config::asio_client::message_type::ptr;
    std::unique_ptr<Client> client_;

    // synchronization using a spin lock
    std::mutex lock;
    RuntimeLock has_message_;
    std::string payload_;

    std::thread bg_client_thread_;
    websocketpp::connection_hdl connection_handle_;
};

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
                                            uint32_t col_num) override {
        SymbolRequest req(SymbolRequest::request_type::get_breakpoints);
        req.filename = filename;
        req.line_num = line_num;
        req.column_num = col_num;

        auto resp = get_resp(req);

        return std::move(resp.bp_results);
    }

    std::vector<BreakPoint> get_breakpoints(const std::string &filename) override {
        SymbolRequest req(SymbolRequest::request_type::get_breakpoints);
        req.filename = filename;

        auto resp = get_resp(req);
        return std::move(resp.bp_results);
    }

    std::optional<BreakPoint> get_breakpoint(uint32_t breakpoint_id) override {
        SymbolRequest req(SymbolRequest::request_type::get_breakpoint);
        req.breakpoint_id = breakpoint_id;

        auto resp = get_resp(req);
        return std::move(resp.bp_result);
    }

    std::optional<std::string> get_instance_name_from_bp(uint32_t breakpoint_id) override {
        SymbolRequest req(SymbolRequest::request_type::get_instance_name_from_bp);
        req.breakpoint_id = breakpoint_id;

        auto resp = get_resp(req);
        return resp.str_result;
    }

    std::optional<std::string> get_instance_name(uint32_t id) override {
        SymbolRequest req(SymbolRequest::request_type::get_instance_name);
        req.instance_id = id;

        auto resp = get_resp(req);
        return resp.str_result;
    }

    std::optional<uint64_t> get_instance_id(const std::string &instance_name) override {
        SymbolRequest req(SymbolRequest::request_type::get_instance_id);
        req.instance_name = instance_name;

        auto resp = get_resp(req);
        return resp.uint64_t_result;
    }

    [[nodiscard]] std::optional<uint64_t> get_instance_id(uint64_t breakpoint_id) override {
        SymbolRequest req(SymbolRequest::request_type::get_instance_id);
        req.breakpoint_id = breakpoint_id;

        auto resp = get_resp(req);
        return resp.uint64_t_result;
    }

    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id) override {
        SymbolRequest req(SymbolRequest::request_type::get_context_variables);
        req.breakpoint_id = breakpoint_id;

        auto resp = get_resp(req);
        return std::move(resp.context_vars_result);
    }

    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) override {
        SymbolRequest req(SymbolRequest::request_type::get_generator_variables);
        req.instance_id = instance_id;

        auto resp = get_resp(req);
        return std::move(resp.gen_vars_result);
    }

    [[nodiscard]] std::vector<std::string> get_instance_names() override {
        SymbolRequest req(SymbolRequest::request_type::get_instance_names);
        auto resp = get_resp(req);
        return resp.str_results;
    }

    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name) override {
        SymbolRequest req(SymbolRequest::request_type::get_annotation_values);
        req.name = name;

        auto resp = get_resp(req);
        return resp.str_results;
    }
    std::unordered_map<std::string, int64_t> get_context_static_values(
        uint32_t breakpoint_id) override {
        SymbolRequest req(SymbolRequest::request_type::get_context_static_values);
        req.breakpoint_id = breakpoint_id;

        auto resp = get_resp(req);
        return resp.map_result;
    }

    std::vector<std::string> get_all_array_names() override {
        SymbolRequest req(SymbolRequest::request_type::get_all_array_names);

        auto resp = get_resp(req);
        return resp.str_results;
    }

    std::vector<std::string> get_filenames() override {
        SymbolRequest req(SymbolRequest::request_type::get_filenames);

        auto resp = get_resp(req);
        return resp.str_results;
    }

    [[nodiscard]] std::optional<std::string> resolve_scoped_name_breakpoint(
        const std::string &scoped_name, uint64_t breakpoint_id) override {
        SymbolRequest req(SymbolRequest::request_type::resolve_scoped_name_breakpoint);
        req.scoped_name = scoped_name;
        req.breakpoint_id = breakpoint_id;

        auto resp = get_resp(req);
        return resp.str_result;
    }

    [[nodiscard]] std::optional<std::string> resolve_scoped_name_instance(
        const std::string &scoped_name, uint64_t instance_id) override {
        SymbolRequest req(SymbolRequest::request_type::resolve_scoped_name_instance);
        req.scoped_name = scoped_name;
        req.instance_id = instance_id;

        auto resp = get_resp(req);
        return resp.str_result;
    }

    // accessors
    [[nodiscard]] std::vector<uint32_t> execution_bp_orders() override {
        // this function is only called once
        SymbolRequest req(SymbolRequest::request_type::get_execution_bp_orders);
        auto resp = get_resp(req);
        return resp.uint64_t_results;
    }

    [[nodiscard]] std::vector<std::tuple<uint32_t, std::string, std::string>>
    get_assigned_breakpoints(const std::string &var_name, uint32_t breakpoint_id) override {
        SymbolRequest req(SymbolRequest::request_type::get_assigned_breakpoints);
        req.name = var_name;
        req.breakpoint_id = breakpoint_id;
        auto resp = get_resp(req);
        return resp.var_result;
    }

    [[nodiscard]] bool bad() const override { return network_ == nullptr; }

    ~NetworkSymbolTableProvider() override = default;

private:
    std::unique_ptr<NetworkProvider> network_;

    hgdb::SymbolResponse get_resp(const hgdb::SymbolRequest &req) {
        SymbolResponse resp(req.req_type());
        if (network_) {
            network_->send(req.str());
            auto str = network_->receive();
            resp.parse(str);
        }

        return resp;
    }
};

enum class FileType { SQLite, JSON, Invalid };

FileType identify_db_format(const std::string &filename) {
    FileType type = FileType::Invalid;
    // read out the first 16 bytes, if any
    std::ifstream s(filename);
    if (s.bad()) return type;
    std::array<char, 16> buffer = {};
    auto size = s.readsome(buffer.data(), 15);
    buffer[15] = 0;  // make sure
    std::string_view sv = buffer.data();
    // https://www.sqlite.org/fileformat.html#magic_header_string
    if (size == 15 && sv == "SQLite format 3") {
        return FileType::SQLite;
    }
    // assume it's json file
    return FileType::JSON;
}

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
        // make sure the filename exists
        if (!std::filesystem::exists(filename)) {
            log::log(log::log_level::error, "Unable to find " + filename);
            return nullptr;
        }

        // identify the database format
        auto type = identify_db_format(filename);
        switch (type) {
            case FileType::SQLite: {
                return std::make_unique<DBSymbolTableProvider>(filename);
            }
            case FileType::JSON: {
                return std::make_unique<JSONSymbolTableProvider>(filename);
            }
            default: {
                // invalid file
                log::log(log::log_level::error, "Invalid symbol table file " + filename);
                return nullptr;
            }
        }
    }
}
}  // namespace hgdb
