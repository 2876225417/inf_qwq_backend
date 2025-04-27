#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H


#include <inferer/chars_ort_inferer.hpp>
#include <database/db_conn.h>
#include <future>
#include <memory>
#include <utility>
#include <utils/rtsp_capturer.h>
#include <json.hpp>

#include <database/db_ops.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields_fwd.hpp>
#include <boost/beast/http/file_body_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/core/ignore_unused.hpp>
#include <iostream>
#include <chrono>
#include <random>


#ifdef USE_PGSQL
#include <pqxx/internal/statement_parameters.hxx>
#endif

#ifdef USE_MYSQL
#include <mysqlx/xdevapi.h>
#endif


namespace inf_qwq::http {
    using json      = nlohmann::json;
    namespace beast = boost::beast;
    namespace http  = beast::http;
    namespace net   = boost::asio;
    using tcp       = boost::asio::ip::tcp;
    using namespace inf_qwq::database;

    /* ----Route Table Parser---- */
    enum class http_method {
        GET,
        POST,
        PUT,
        DELETE,
        UNKNOWN
    };

    inline http_method enum2method(http::verb method) {
        switch (method) {
            case http::verb::get:       return http_method::GET;
            case http::verb::post:      return http_method::POST;
            case http::verb::put:       return http_method::PUT;
            case http::verb::delete_:   return http_method::DELETE;
            default:                    return http_method::UNKNOWN;
        }
    }
    
    enum class api_route {
        HELLO,
        GET_ALL_RTSP_SOURCES,
        HEALTH,
        ADD_RTSP_SOURCE,
        UPDATE_CROPPED_COORDS,
        GET_INF_RESULT,
        REMOVE_RTSP_STREAM,
        UNKNOWN
    };

    struct route_info {
        std::string_view path;
        http_method method;
        api_route route;
    };

    template <size_t N>
    struct static_route_table {
        std::array<route_info, N> routes;

        template <size_t... I>
        constexpr static_route_table(const route_info (&init)[N], std::index_sequence<I...>)
            : routes{init[I]...} {}

        constexpr static_route_table(const route_info (&init)[N])
            : static_route_table(init, std::make_index_sequence<N>{}) {}
    
        constexpr api_route find(std::string_view path, http_method method) const {
            for (const auto& route: routes) 
                if (route.path == path && route.method == method) return route.route;   
            return api_route::UNKNOWN;
        }
    };

    static constexpr route_info route_definitions[] = {
        {"/hello",                          http_method::GET,   api_route::HELLO},
        {"/inf_qwq/get_all_rtsp_sources",   http_method::GET,   api_route::GET_ALL_RTSP_SOURCES},
        {"/inf_qwq/health",                 http_method::GET,   api_route::HEALTH},
        {"/inf_qwq/add_rtsp_source",        http_method::POST,  api_route::ADD_RTSP_SOURCE},
        {"/inf_qwq/update_cropped_coords",  http_method::POST,  api_route::UPDATE_CROPPED_COORDS},
        {"/inf_qwq/get_inf_result",         http_method::GET,   api_route::GET_INF_RESULT},
        {"/inf_qwq/remove_rtsp_stream",     http_method::POST,  api_route::REMOVE_RTSP_STREAM},
    };
    static constexpr auto route_table = static_route_table(route_definitions);
    /* ----Route Table Parser---- */

    static const auto server_start_time = std::chrono::steady_clock::now();

    inline long get_server_uptime() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - server_start_time).count();
    }

    class http_connection;

    void handle_hello(http_connection& conn);
    void handle_health_check(http_connection& conn);
    void handle_get_all_rtsp_sources(http_connection& conn);
    void handle_add_rtsp_source(http_connection& conn) ;
    void handle_update_cropped_coords(http_connection& conn);
    void handle_not_found(http_connection& conn);
    void handle_get_inf_result(http_connection& conn);
    void handle_remove_rtsp_stream(http_connection& conn);

    using route_handler_func = std::function<void(http_connection&)>;
    inline const std::unordered_map<api_route, route_handler_func>& get_route_handlers() {
        static const std::unordered_map<api_route, route_handler_func> handlers = {
            {api_route::HELLO, handle_hello},
            {api_route::HEALTH, handle_health_check},
            {api_route::GET_ALL_RTSP_SOURCES, handle_get_all_rtsp_sources},
            {api_route::ADD_RTSP_SOURCE, handle_add_rtsp_source},
            {api_route::GET_INF_RESULT, handle_get_inf_result},
            {api_route::REMOVE_RTSP_STREAM, handle_remove_rtsp_stream},
            {api_route::UNKNOWN, handle_not_found},
        };
        return handlers;
    }

    class http_connection: public std::enable_shared_from_this<http_connection> {
    public:     
        http_connection(tcp::socket socket): m_socket(std::move(socket)) {}
        void start() { read_request(); }

        tcp::socket& socket() { return m_socket; }
        http::request<http::string_body>& request() { return m_request; }
        http::response<http::string_body>& response() { return m_response; }

    private: 
        tcp::socket                         m_socket;
        beast::flat_buffer                  m_buffer{8192};
        http::request<http::string_body>    m_request;
        http::response<http::string_body>   m_response;

        void read_request() {
            auto self = shared_from_this();
                
            http::async_read( m_socket
                            , m_buffer
                            , m_request
                            , [self]( beast::error_code ec
                                            , std::size_t bytes_transferred) { 
                                                boost::ignore_unused(bytes_transferred);
                                                if (!ec) self->process_request();
                                                else std::cerr << "Error reading request :" << ec.message() << "\n";
                                            }); 
        }

        void process_request() {
            m_response.version(m_request.version());
            m_response.keep_alive(false);
            std::string_view target(m_request.target().data(), m_request.target().size());
            http_method method = enum2method(m_request.method());
            api_route route = route_table.find(target, method);
            m_response.set(http::field::server, "Beast");
            dispatch_route(route);
            write_response();
        }

        void dispatch_route(api_route route) {
            const auto& handlers = get_route_handlers();
            auto it = handlers.find(route);
            if (it != handlers.end()) it->second(*this);
            else handle_not_found(*this);
        }

        void handle_not_found(http_connection& conn) {
            auto& m_response = conn.response();
            m_response.result(http::status::not_found);
            m_response.set(http::field::content_type, "application/json");
            m_response.body() = "Endpoint not found\r\n";   
        }


        void write_response() { 
            auto self = shared_from_this();    
            m_response.content_length(m_response.body().size());
                
            http::async_write( m_socket, m_response
                             , [self]( beast::error_code ec
                                    , std::size_t ) {
                                        if (ec) std::cerr << "Error writing response: " << ec.message() << "\n";
                                            self->m_socket.shutdown(tcp::socket::shutdown_send, ec);
                                    });
            }
        };

        inline void handle_hello(http_connection& conn) { 
            auto& m_response = conn.response();
            m_response.set(http::field::content_type, "text/plain");
            m_response.body() = "Hello C++";
        }

        inline void handle_health_check(http_connection& conn) {
            auto& m_response = conn.response();
            try {
                bool database_connected = false;
                try {
                    #ifdef USE_MYSQL
                    auto result = execute_query("SELECT 1");
                    database_connected = (result.count() > 0);
                    #endif
                    #ifdef USE_PGSQL
                    pqxx::result result = execute_query("SELECT 1");
                    database_connected = !result.empty();
                    #endif
                } catch (const std::exception& e) {
                    database_connected = false;
                }
        
                nlohmann::json response_json;
                response_json["status"] = "ok";
                response_json["timestamp"] = std::time(nullptr);
                response_json["service"] = "rtsp-monitor-server";
                response_json["database_connected"] = database_connected;
        
                m_response.result(http::status::ok);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = response_json.dump();
        
                std::cout << "Health check request processed" << std::endl;
            } catch (const std::exception& e) {
                nlohmann::json error_json;
                error_json["status"] = "error";
                error_json["error"] = "Error processing health check: " + std::string(e.what());
                
                m_response.result(http::status::internal_server_error);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
        
                std::cerr << "Error during health check: " << e.what();
            }
        }

        inline void handle_get_all_rtsp_sources(http_connection& conn) {
            auto& m_response = conn.response(); 
            try {
                std::cout << "GET request to /inf_qwq/get_all_rtsp_sources" << std::endl;
                
                #ifdef USE_MYSQL
                auto result = execute_query(
                    "SELECT rtsp_id, rtsp_type, rtsp_username, rtsp_id, rtsp_port, "
                    "rtsp_channel, rtsp_subtype, rtsp_url, rtsp_name, "
                    "rtsp_crop_coord_x, rtsp_crop_coord_y, rtsp_crop_coord_dx, rtsp_crop_coord_dy "
                    "FROM rtsp_stream_info "
                    "ORDER BY rtsp_id"
                );
        
                nlohmann::json response_json;
                response_json["success"] = true;
                response_json["rtsp_sources"] = nlohmann::json::array();
        
                while (auto row = result.fetchOne()) {
                    nlohmann::json source;
                    
                    source["rtsp_id"]       = row[0].get<int>();
                    source["rtsp_type"]     = row[1].isNull() ? "" : row[1].get<std::string>();
                    source["rtsp_username"] = row[2].get<std::string>();
                    source["rtsp_id"]       = row[3].get<std::string>();
                    source["rtsp_port"]     = row[4].get<int>();
                    source["rtsp_channel"]  = row[5].get<std::string>();
                    source["rtsp_subtype"]  = row[6].get<std::string>();
                    source["rtsp_url"]      = row[7].get<std::string>();
                    source["rtsp_name"]     = row[8].get<std::string>();
                    if (!row[9].isNull()) source["rtsp_crop_coord_x"] = row[9].get<float>();
                    if (!row[10].isNull()) source["rtsp_crop_coord_x"] = row[10].get<float>();
                    if (!row[11].isNull()) source["rtsp_crop_coord_x"] = row[11].get<float>();
                    if (!row[12].isNull()) source["rtsp_crop_coord_x"] = row[12].get<float>();
                    response_json["rtsp_sources"].push_back(source);
                }
        
                m_response.result(http::status::ok);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = response_json.dump();
        
                std::cout << "Returned " << response_json["rtsp_sources"].size() << " RTSP sources" << std::endl;
                #endif
        
                #ifdef USE_PGSQL
                std::string sql = 
                    "SELECT rtsp_id, rtsp_type, rtsp_username, rtsp_ip, rtsp_port, "
                    "rtsp_channel, rtsp_subtype, rtsp_url, rtsp_name, "
                    "rtsp_crop_coord_x, rtsp_crop_coord_y, rtsp_crop_coord_dx, rtsp_crop_coord_dy "
                    "FROM rtsp_stream_info "
                    "ORDER BY rtsp_id";
        
                pqxx::result result = execute_query(sql);
        
                nlohmann::json response_json;
                response_json["success"] = true;
                response_json["rtsp_sources"] = nlohmann::json::array();
        
                for (const auto& row: result) {
                    nlohmann::json source;
                    
                    source["rtsp_id"] = row["rtsp_id"].as<int>();
                    source["rtsp_type"] = row["rtsp_type"].is_null() ? "" : row["rtsp_type"].as<std::string>();
                    source["rtsp_username"] = row["rtsp_username"].as<std::string>();
                    source["rtsp_ip"] = row["rtsp_port"].as<std::string>();
                    source["rtsp_port"] = row["rtsp_port"].as<int>();
                    source["rtsp_channel"] = row["rtsp_channel"].as<std::string>();
                    source["rtsp_subtype"] = row["rtsp_subtype"].as<std::string>();
                    source["rtsp_url"] = row["rtsp_url"].as<std::string>();
                    source["rtsp_name"] = row["rtsp_name"].as<std::string>();
        
                    if (!row["rtsp_crop_coord_x"].is_null()) source["rtsp_crop_coord_x"]   = row["rtsp_crop_coord_x"].as<float>();
                    if (!row["rtsp_crop_coord_y"].is_null()) source["rtsp_crop_coord_y"]   = row["rtsp_crop_coord_y"].as<float>();
                    if (!row["rtsp_crop_coord_dx"].is_null()) source["rtsp_crop_coord_dx"] = row["rtsp_crop_coord_dx"].as<float>();
                    if (!row["rtsp_crop_coord_dy"].is_null()) source["rtsp_crop_coord_dy"] = row["rtsp_crop_coord_dy"].as<float>();
        
                    response_json["rtsp_sources"].push_back(source);
                }
        
                m_response.result(http::status::ok);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = response_json.dump();
        
                std::cout << "Returned " << result.size() << " RTSP sources" << std::endl;         
                #endif
            } catch (const database_exception& e) {
                nlohmann::json error_json;
                error_json["success"] = false;
                error_json["error"] = "Database erro: " + std::string(e.what());
                
                m_response.result(http::status::internal_server_error);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
        
                std::cerr << "Database error: " << e.what() << std::endl;
            } catch (const std::exception& e) {
                nlohmann::json error_json;
                error_json["success"] = false;
                error_json["error"] = "Error: " + std::string(e.what());
            
                m_response.result(http::status::internal_server_error);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
                    
                std::cerr << "Error: " << e.what() << std::endl;
            }
        }

        // inline void handle_add_rtsp_source(http_connection& conn) {
        //     auto& m_response = conn.response();
        //     auto& m_request = conn.request();
        //     try {
        //         std::string body = m_request.body();    
        //         auto json = nlohmann::json::parse(body);
        //         // check url existed
        //         std::string rtsp_url = json.value("rtsp_url", "");
        //         if (rtsp_url.empty())
        //             throw std::runtime_error("rtsp_url can not be empty");
                
        //         #ifdef USE_MYSQL
        //         auto check_result = execute_params(
        //             "SELECT rtsp_id FROM rtsp_stream_info WHERE rtsp_url = ?",
        //             rtsp_url
        //         );

        //         if (check_result.count() > 0) {
        //             auto row = check_result.fetchOne();
        //             int existing_rtsp_id = row[0].get<int>();

        //             nlohmann::json response_json;
        //             response_json["success"] = false;
        //             response_json["error"]   = "RTSP URL already exists";
        //             response_json["existing_rtsp_id"] = existing_rtsp_id;
        //             response_json["message"] = "A RTSP source with this URL already exists";

        //             m_response.result(http::status::conflict);
        //             m_response.set(http::field::content_type, "application/json");
        //             m_response.body() = response_json.dump();
                    
        //             std::cout << "RTSP URL already exists with ID: " << existing_rtsp_id << std::endl;
        //         } else {
        //             auto result = execute_params(
        //                 "INSERT INTO rtsp_stream_info ("
        //                 "rtsp_type, rtsp_username, rtsp_ip, rtsp_port, rtsp_channel, "
        //                 "rtsp_subtype, rtsp_url, rtsp_name) "
        //                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        //                 json.value("rtsp_type", ""),
        //                 json.value("rtsp_username", ""),
        //                 json.value("rtsp_ip", ""),
        //                 json.value("rtsp_port", 0),
        //                 json.value("rtsp_channel", ""),
        //                 json.value("rtsp_subtype", ""),
        //                 rtsp_url,
        //                 json.value("rtsp_name", "无")
        //             );

        //             auto id_result = execute_query("SELECT LAST_INSERT_ID()");
        //             auto id_row = id_result.fetchOne();
        //             auto rtsp_id = id_row[0].get<int>();

        //             nlohmann::json response_json;
        //             response_json["success"] = true;
        //             response_json["rtsp_id"] = rtsp_id;
        //             response_json["message"] = "RTSP source added successfully";
                
        //             m_response.result(http::status::ok);
        //             m_response.set(http::field::content_type, "application/json");
        //             m_response.body() = response_json.dump();

        //             std::cout << "New RTSP source added with ID: " << rtsp_id << std::endl;
        //         }
        //         #endif

        //         #ifdef USE_PGSQL
        //         std::string check_rtsp_url_existed_sql = 
        //             "SELECT rtsp_id from rtsp_stream_info WHERE rtsp_url = $1";
                    
        //         pqxx::result check_result = execute_params(check_rtsp_url_existed_sql, rtsp_url);
                    
        //             if (!check_result.empty()) {
        //                 int existing_rtsp_id = check_result[0][0].as<int>();
                    
        //                 nlohmann::json response_json;
        //                 response_json["success"] = false;
        //                 response_json["error"] = "RTSP URL already exists";
        //                 response_json["existing_rtsp_id"] = existing_rtsp_id;
        //                 response_json["message"] = "An RTSP source with this URL already exists";

        //                 m_response.result(http::status::conflict);  // 409 conflict
        //                 m_response.set(http::field::content_type, "application/json");
                        
        //                 m_response.body() = response_json.dump();
        //                 std::cout << "RTSP URL already exists with ID: " << existing_rtsp_id << std::endl;
        //             } else {
                
        //                 std::string sql = 
        //                     "INSERT INTO rtsp_stream_info ("
        //                     "rtsp_type, rtsp_username, rtsp_ip, rtsp_port, rtsp_channel, "
        //                     "rtsp_subtype, rtsp_url, rtsp_name, "
        //                     "rtsp_crop_coord_x, rtsp_crop_coord_y, rtsp_crop_coord_dx, rtsp_crop_coord_dy) "
        //                     "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12) "
        //                     "RETURNING rtsp_id";
                    
        //                 pqxx::result result = execute_params( sql
        //                                                     , json.value("rtsp_type", "")
        //                                                     , json.value("rtsp_username", "")
        //                                                     , json.value("rtsp_ip", "")
        //                                                     , json.value("rtsp_port", 0)
        //                                                     , json.value("rtsp_channel", "")
        //                                                     , json.value("rtsp_subtype", "")
        //                                                     , rtsp_url
        //                                                     , json.value("rtsp_name", "无")
        //                                                     , 0.f, 0.f, 1.f, 1.f
        //                                                     );
        //                 int rtsp_id = result[0][0].as<int>();
                        
        //                 // if inserted
        //                 nlohmann::json response_json;
        //                 response_json["successs"] = true;
        //                 response_json["rtsp_id"] = rtsp_id;
        //                 response_json["message"] = "RTSP source added successfully";
        //                 // response
        //                 m_response.result(http::status::ok);
        //                 m_response.set(http::field::content_type, "application/json");
        //                 m_response.body() = response_json.dump();

        //                 std::cout << "New RTSP source added with ID: " << rtsp_id << std::endl;

        //                 std::string rtsp_name_copy = json.value("rtsp_name", "无");
        //                 std::string rtsp_type_copy = json.value("rtsp_type", "");
        //                 std::string rtsp_username_copy = json.value("rtsp_username", "");
        //                 std::string rtsp_ip_copy = json.value("rtsp_ip", "");
        //                 int rtsp_port_copy = json.value("rtsp_port", 0);
        //                 std::string rtsp_channel_copy = json.value("rtsp_channel", "");
        //                 std::string rtsp_subtype_copy = json.value("rtsp_subtype", "");
                        
        //                 std::thread start_stream_thread([rtsp_id, rtsp_url, rtsp_name_copy, rtsp_type_copy, rtsp_username_copy, rtsp_ip_copy, rtsp_port_copy, rtsp_channel_copy, rtsp_subtype_copy]() {
        //                     try {
        //                         auto& capturer = utils::rtsp::rtsp_capturer::instance();
        //                         bool started = capturer.add_rtsp_stream( rtsp_id, rtsp_url
        //                                                        , rtsp_name_copy
        //                                                        , 0.f, 0.f, 1.f, 1.f
        //                                                        , rtsp_type_copy
        //                                                        , rtsp_username_copy
        //                                                        , rtsp_ip_copy
        //                                                        , rtsp_port_copy
        //                                                        , rtsp_channel_copy
        //                                                        , rtsp_subtype_copy
        //                                                        );
        //                         std::cout << "RTSP stream with ID " << rtsp_id << (started ? "started" : "failed to start") << std::endl;
        //                     } catch (const std::exception& e) {
        //                         std::cerr << "Error starting RTSP stream: " << e.what() << std::endl;
        //                     }
        //                 });
        //                 start_stream_thread.detach();
        //             }
        //         #endif
        //         } catch (const nlohmann::json::exception& e) {
        //             nlohmann::json error_json;
        //             error_json["success"] = false;
        //             error_json["error"] = "Invalid JSON format: " + std::string(e.what());
                    
        //             m_response.result(http::status::bad_request);
        //             m_response.set(http::field::content_type, "application/json");
        //             m_response.body() = error_json.dump();
                    
        //             std::cerr << "JSON parsing error: " << e.what() << std::endl;
        //         } catch (const database_exception& e) {
        //             nlohmann::json error_json;
        //             error_json["success"] = false;
        //             error_json["error"] = "Database error: " + std::string(e.what());

        //             m_response.result(http::status::internal_server_error);
        //             m_response.set(http::field::content_type, "application/json");
        //             m_response.body() = error_json.dump();
        //         } catch (const std::exception& e) {
        //             nlohmann::json error_json;
        //             error_json["success"] = false;
        //             error_json["error"] = "Error: " + std::string(e.what());
                    
        //             m_response.result(http::status::internal_server_error);
        //             m_response.set(http::field::content_type, "application/json");
        //             m_response.body() = error_json.dump();
                    
        //             std::cerr << "Error: " << e.what() << std::endl;
        //         } 
        // }

        inline void handle_add_rtsp_source(http_connection& conn) {
            auto& m_response = conn.response();
            auto& m_request = conn.request();
            try {
                // 解析请求体中的 JSON 数据
                std::string body = m_request.body();    
                auto json = nlohmann::json::parse(body);
                
                // 检查必要字段
                std::string rtsp_url = json.value("rtsp_url", "");
                if (rtsp_url.empty()) {
                    throw std::runtime_error("rtsp_url cannot be empty");
                }
                
                // 获取其他字段，设置默认值
                int rtsp_id = json.value("rtsp_id", 0);  // 如果未提供ID，使用0或生成一个随机ID
                if (rtsp_id <= 0) {
                    // 生成一个随机ID
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> distrib(1, 1000000);
                    rtsp_id = distrib(gen);
                }
                
                std::string rtsp_name = json.value("rtsp_name", "RTSP Stream " + std::to_string(rtsp_id));
                float crop_x = json.value("crop_x", 0.0f);
                float crop_y = json.value("crop_y", 0.0f);
                float crop_dx = json.value("crop_dx", 1.0f);
                float crop_dy = json.value("crop_dy", 1.0f);
                std::string rtsp_type = json.value("rtsp_type", "");
                std::string rtsp_username = json.value("rtsp_username", "");
                std::string rtsp_ip = json.value("rtsp_ip", "");
                int rtsp_port = json.value("rtsp_port", 0);
                std::string rtsp_channel = json.value("rtsp_channel", "");
                std::string rtsp_subtype = json.value("rtsp_subtype", "");
                
                // 立即响应客户端
                nlohmann::json response_json;
                response_json["success"] = true;
                response_json["rtsp_id"] = rtsp_id;
                response_json["message"] = "Adding RTSP stream in background";
                
                m_response.result(http::status::ok);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = response_json.dump();
                
                std::cout << "Request to add RTSP stream with ID: " << rtsp_id 
                          << ", URL: " << rtsp_url << std::endl;
                
                // 创建一个完全独立的线程来添加RTSP流
                std::thread* start_thread = new std::thread([rtsp_id, rtsp_url, rtsp_name, 
                                                            crop_x, crop_y, crop_dx, crop_dy,
                                                            rtsp_type, rtsp_username, rtsp_ip, 
                                                            rtsp_port, rtsp_channel, rtsp_subtype]() {
                    try {
                        // 添加足够的延迟，确保HTTP响应已完全发送
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        
                        std::cout << "Starting to add RTSP stream in background thread, ID: " << rtsp_id << std::endl;
                        
                        // 获取rtsp_capturer单例
                        auto& capturer = inf_qwq::utils::rtsp::rtsp_capturer::instance();
                        
                        // 尝试添加RTSP流
                        bool started = capturer.add_rtsp_stream(
                            rtsp_id, rtsp_url, rtsp_name,
                            crop_x, crop_y, crop_dx, crop_dy,
                            rtsp_type, rtsp_username, rtsp_ip,
                            rtsp_port, rtsp_channel, rtsp_subtype
                        );
                        
                        std::cout << "RTSP stream with ID " << rtsp_id 
                                  << (started ? " added successfully" : " failed to add") << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "Error adding RTSP stream: " << e.what() << std::endl;
                    }
                    
                    // 清理线程资源
                    // delete std::this_thread::get_id();
                });
                
                // 分离线程，让它在后台运行
                start_thread->detach();
                std::cout << "Background thread created and detached for adding RTSP stream with ID: " 
                          << rtsp_id << std::endl;
                
            } catch (const nlohmann::json::exception& e) {
                // JSON解析错误
                nlohmann::json error_json;
                error_json["success"] = false;
                error_json["error"] = "Invalid JSON format: " + std::string(e.what());
                
                m_response.result(http::status::bad_request);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
                
                std::cerr << "JSON parsing error: " << e.what() << std::endl;
            } catch (const std::exception& e) {
                // 其他错误
                nlohmann::json error_json;
                error_json["success"] = false;
                error_json["error"] = "Error: " + std::string(e.what());
                
                m_response.result(http::status::internal_server_error);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
                
                std::cerr << "Error: " << e.what() << std::endl;
            }
        }

        inline void handle_update_cropped_coords(http_connection& conn) {
            auto& m_response = conn.response();
            auto& m_request = conn.request();
            
            try {
                std::string body = m_request.body();
                std::cout << "Post to /inf_qwq/update_cropped_coords: " << body << std::endl;
                
                auto json = nlohmann::json::parse(body);
        
                int rtsp_id = json["rtsp_id"];
                float x = json["x"];
                float y = json["y"];
                float dx = json["dx"];
                float dy = json["dy"];
                
                #ifdef USE_MYSQL
                auto check_result = execute_params(
                    "SELECT rtsp_id FROM rtsp_stream_info WHERE rtsp_id = ?",
                    rtsp_id
                );
        
                if (check_result.count() == 0) {
                    nlohmann::json error_json;
                    error_json["success"] = false;
                    error_json["error"] = "RTSP ID not found";
                    error_json["message"] = "No RTSP source exist with the provided ID: ";
        
                    m_response.result(http::status::not_found);
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = error_json.dump();
                    
                    std::cout << "RTSP ID not found: " << rtsp_id << std::endl;
                } else {
                    execute_params(
                        "UPDATE rtsp_stream_info "
                        "SET rtsp_crop_coord_x = ?, rtsp_crop_coord_y = ?, "
                        "rtsp_crop_coord_dx = ?, rtsp_crop_coord_dy = ? "
                        "WHERE rtsp_id = ?",
                        x, y, dx, dy, rtsp_id
                    );
        
                    inf_qwq::utils::rtsp::rtsp_capturer::instance().update_crop_coordinates(rtsp_id, x, y, dx, dy);
                    
                    nlohmann::json response_json;
                    response_json["success"] = true;
                    response_json["rtsp_id"] = rtsp_id;
                    response_json["message"] = "Cropped coordinates updated successfully";
                    
                    m_response.result(http::status::ok);
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = response_json.dump();
        
                    std::cout << "Updated cropped coordinates for RTSP ID: " << rtsp_id
                              << " [x= " << x
                              << ", y= " << y
                              << ", dx=" << dx
                              << ", dy=" << dy
                              << "]"     << std::endl;
                }
                #endif
        
                #ifdef USE_PGSQL
                std::string check_sql = "SELECT rtsp_id FROM rtsp_stream_info WHERE rtsp_id = $1";
                pqxx::result check_result = execute_params(check_sql, rtsp_id);
        
                if (check_sql.empty()) {
                    nlohmann::json error_json;
                    error_json["success"] = false;
                    error_json["error"] = "RTSP ID not found";
                    error_json["message"] = "No RTSP source exists with the provided ID";
                    
        
                    m_response.result(http::status::not_found); // 404 NOT FOUND
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = error_json.dump();
        
                    std::cout << "RTSP ID not found: " << rtsp_id << std::endl;
                } else {
                    std::string update_sql =
                        "UPDATE rtsp_stream_info "
                        "SET rtsp_crop_coord_x = $1, rtsp_crop_coord_y = $2, "
                        "rtsp_crop_coord_dx = $3, rtsp_crop_coord_dy = $4 "
                        "WHERE rtsp_id = $5";
        
                    execute_params(update_sql, x, y, dx, dy, rtsp_id);
                    
                    inf_qwq::utils::rtsp::rtsp_capturer::instance().update_crop_coordinates(rtsp_id, x, y, dx,  dy);
                    
                    nlohmann::json response_json;
                    response_json["success"] = true;
                    response_json["rtsp_id"] = rtsp_id;
                    response_json["message"] = "Cropped coordinates updated successfully";
                    
                    m_response.result(http::status::ok);
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = response_json.dump();
                    
                    std::cout << "Updated cropped coordinates for RTSP ID: " << rtsp_id
                              << " [x="  << x 
                              << ", y="  << y 
                              << ", dx=" << dx 
                              << ", dy=" << dy 
                              << "]"     << std::endl;
            
                } 
                #endif
            } catch (const nlohmann::json::exception& e) {
                nlohmann::json error_json;
                error_json["success"] = false;
                error_json["error"] = "Invalid Json format: " + std::string(e.what());
                
                m_response.result(http::status::bad_request);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
        
                std::cerr << "JSON parsing error: " << e.what() << std::endl;
            } catch (const database_exception& e) {
                nlohmann::json error_json;
                error_json["success"] = false;
                error_json["error"] = "Database error: " + std::string(e.what());
                
                m_response.result(http::status::internal_server_error);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
                
                std::cerr << "Database error: " << e.what() << std::endl;
            } catch (const std::exception& e) {
                nlohmann::json error_json;
                error_json["success"] = false;
                error_json["error"] = "Error: " + std::string(e.what());
                
                m_response.result(http::status::internal_server_error);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
        
                std::cerr << "Error: " << e.what() << std::endl;
            }  
        }

        using namespace utils::rtsp;
        inline void handle_get_inf_result(http_connection& conn) {
            auto& m_response = conn.response();
            try {
                auto& capturer = rtsp_capturer::instance();
                
                extern std::shared_ptr<chars_ort_inferer> g_ort_inferer;
                
                if (!g_ort_inferer) {
                    nlohmann::json error_json;
                    error_json["status"] = "error";
                    error_json["message"] = "Inference engine not initialized";
                    
                    m_response.result(http::status::internal_server_error);
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = error_json.dump();
                    return;
                }
        
                std::promise<std::vector<std::pair<int, std::vector<std::string>>>> result_promise;
                std::future<std::vector<std::pair<int, std::vector<std::string>>>> result_future = result_promise.get_future();
        
                auto original_callback = g_ort_inferer->get_completion_callback();
        
                std::vector<std::pair<int, std::vector<std::string>>> all_results;
                std::mutex results_mutex;
                std::atomic<int> completed_count{0};
                std::atomic<int> expected_count{0};
        
                g_ort_inferer->set_completion_callback([ &all_results, &results_mutex, &result_promise
                                                 , &completed_count, &expected_count, &original_callback](int cam_id, const std::vector<std::string>& texts) {
                                                    if (original_callback) original_callback(cam_id, texts);
        
                                                    {
                                                        std::lock_guard<std::mutex> lock(results_mutex);
                                                        all_results.emplace_back(cam_id, texts);
                                                    }
        
                                                    if (++completed_count >= expected_count) {
                                                        std::lock_guard<std::mutex> lock(results_mutex);
                                                        result_promise.set_value(all_results);
                                                    }
                                                 });
    
                std::vector<captured_image> latest_images = capturer.get_all_latest_images();
                
                if (latest_images.empty()) {
                    nlohmann::json error_json;
                    error_json["status"] = "error";
                    error_json["message"] = "No images available for inference";
        
                    m_response.result(http::status::service_unavailable);
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = error_json.dump();
        
                    g_ort_inferer->set_completion_callback(original_callback);
                    return;
                }
                
                std::vector<std::pair<int, cv::Mat>> frames;
                frames.reserve(latest_images.size());
                for (const auto& image : latest_images) {
                    frames.emplace_back(image.rtsp_id, image.cropped_image.clone());
                }
                
                expected_count = frames.size();
                
                g_ort_inferer->run_inf_batch(frames);
                
                auto status = result_future.wait_for(std::chrono::seconds(5));
        
                g_ort_inferer->set_completion_callback(original_callback);
        
                if (status == std::future_status::ready) {
                    auto results = result_future.get();
                    
                    nlohmann::json response_json;
                    response_json["status"] = "success";
                    response_json["results"] = nlohmann::json::array();
        
                    for (const auto& [cam_id, texts]: results) {
                        nlohmann::json cam_result;
                        cam_result["camera_id"] = cam_id;
                        cam_result["texts"] = texts;
                        response_json["results"].push_back(cam_result);
                    }
        
                    m_response.result(http::status::ok);
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = response_json.dump();
                } else {
                    nlohmann::json error_json;
                    error_json["status"] = "error";
                    error_json["message"] = "Inference timed out";
        
                    m_response.result(http::status::request_timeout);
                    m_response.set(http::field::content_type, "application/json");
                    m_response.body() = error_json.dump();
                }
        
                g_ort_inferer->wait_for_completion(5000);
            } catch (const std::exception& e) {
                nlohmann::json error_json;
                error_json["status"] = "error";
                error_json["message"] = std::string("Inference error") + e.what();
                
                m_response.result(http::status::internal_server_error);
                m_response.set(http::field::content_type, "application/json");
                m_response.body() = error_json.dump();
            }
        }

        inline void handle_remove_rtsp_stream(http_connection& conn) {

        }

        inline void handle_not_found(http_connection& conn) {

        }

    





}   // NAMESPACE INF_QWQ
#endif // HTTP_CONNECTION_H
