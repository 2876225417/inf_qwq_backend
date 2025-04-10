#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include "database/db_conn.h"
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/file_body_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/core/ignore_unused.hpp>
#include <exception>
#include <iostream>

#include <database/db_ops.hpp>

#include <json.hpp>
#include <opencv4/opencv2/core.hpp>
#include <pqxx/internal/statement_parameters.hxx>

namespace inf_qwq {
    namespace http {
        using json = nlohmann::json;
        using namespace database::pg_sql;
        namespace beast = boost::beast;
        namespace http  = beast::http;
        namespace net   = boost::asio;
        using tcp       = boost::asio::ip::tcp;
         
        class http_connection: public std::enable_shared_from_this<http_connection> {
        public: http_connection(tcp::socket socket): m_socket(std::move(socket)) {}
        
        void start() { read_request(); }


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

                switch (m_request.method()) {
                    case http::verb::get:
                        m_response.result(http::status::ok);
                        m_response.set(http::field::server, "Beast");
                        create_response();
                        break;
                    case http::verb::post:
                        handle_post();
                        break;
                    default:
                        m_response.result(http::status::bad_request);
                        m_response.set(http::field::content_type, "text/plain");
                        m_response.body() = "Invalid request-method '" 
                                          + std::string(m_request.method_string()) 
                                          + "'";
                        break;
                
                }
                    write_response();
            }

            void handle_post() {
                m_response.result(http::status::ok);
                m_response.set(http::field::server, "Beast");
                m_response.set(http::field::content_type, "text/plain");

                if (m_request.target() == "/submit") {
                    std::string body = m_request.body();
                    m_response.body() = "Received POST data: " + body;
                    std::cout << m_response.body();
                } else if (m_request.target() == "/inf_qwq/update_cropped_coords") {
                    std::string body = m_request.body();
                    m_response.body() = "Received updated_cropped_coords data: " + body;
                    std::cout << "POST to /inf_qwq/update_cropped_coords: " << body << std::endl;
                } else if (m_request.target() == "/inf_qwq/add_rtsp_source") {
                    try {
                        std::string body = m_request.body();
                        
                        auto json = json::parse(body);
                        
                        // check url existed
                        
                        std::string rtsp_url = json.value("rtsp_url", "");

                        if (rtsp_url.empty())
                            throw std::runtime_error("rtsp_url can not be empty");
                    
                        std::string check_rtsp_url_existed_sql = 
                            "SELECT rtsp_id from rtsp_stream_info WHERE rtsp_url = $1";
                        
                        pqxx::result check_result = execute_params(check_rtsp_url_existed_sql, rtsp_url);
                        
                        if (!check_result.empty()) {
                            int existing_rtsp_id = check_result[0][0].as<int>();
                        
                            nlohmann::json response_json;
                            response_json["success"] = false;
                            response_json["error"] = "RTSP URL already exists";
                            response_json["existing_rtsp_id"] = existing_rtsp_id;
                            response_json["message"] = "An RTSP source with this URL already exists";

                            m_response.result(http::status::conflict);  // 409 conflict
                            m_response.set(http::field::content_type, "application/json");
                            m_response.body() = response_json.dump();

                            std::cout << "RTSP URL already exists with ID: " << existing_rtsp_id << std::endl;
                        } else {
                    
                            std::string sql = 
                                "INSERT INTO rtsp_stream_info ("
                                "rtsp_type, rtsp_username, rtsp_ip, rtsp_port, rtsp_channel, "
                                "rtsp_subtype, rtsp_url, rtsp_name) "
                                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
                                "RETURNING rtsp_id";
                        
                            pqxx::result result = execute_params( sql
                                                                , json.value("rtsp_type", "")
                                                                , json.value("rtsp_username", "")
                                                                , json.value("rtsp_ip", "")
                                                                , json.value("rtsp_port", 0)
                                                                , json.value("rtsp_channel", "")
                                                                , json.value("rtsp_subtype", "")
                                                                , rtsp_url
                                                                , json.value("rtsp_name", "无")
                                                                );
                            int rtsp_id = result[0][0].as<int>();
                        
                            // if inserted
                            nlohmann::json response_json;
                            response_json["successs"] = true;
                            response_json["rtsp_id"] = rtsp_id;
                            response_json["message"] = "RTSP source added successfully";
                        
                            // response
                            m_response.result(http::status::ok);
                            m_response.set(http::field::content_type, "application/json");
                            m_response.body() = response_json.dump();

                            std::cout << "New RTSP source added with ID: " << rtsp_id << std::endl;
                        }
                    } catch (const nlohmann::json::exception& e) {
                        nlohmann::json error_json;
                        error_json["success"] = false;
                        error_json["error"] = "Invalid JSON format: " + std::string(e.what());
                        
                        m_response.result(http::status::bad_request);
                        m_response.set(http::field::content_type, "application/json");
                        m_response.body() = error_json.dump();
                        
                        std::cerr << "JSON parsing error: " << e.what() << std::endl;
                    } catch (const pg_sql_exception& e) {
                        nlohmann::json error_json;
                        error_json["success"] = false;
                        error_json["error"] = "Database error: " + std::string(e.what());

                        m_response.result(http::status::internal_server_error);
                        m_response.set(http::field::content_type, "application/json");
                        m_response.body() = error_json.dump();
                    } catch (const std::exception& e) {
                        nlohmann::json error_json;
                        error_json["success"] = false;
                        error_json["error"] = "Error: " + std::string(e.what());
                        
                        m_response.result(http::status::internal_server_error);
                        m_response.set(http::field::content_type, "application/json");
                        m_response.body() = error_json.dump();
                        
                        std::cerr << "Error: " << e.what() << std::endl;
                    } 
                } else if (m_request.target() == "/inf_qwq/update_cropped_coords"){
                    try {
                        std::string body = m_request.body();
                        std::cout << "Post to /inf_qwq/update_cropped_coords: " << body << std::endl;
                        
                        auto json = nlohmann::json::parse(body);

                        int rtsp_id = json["rtsp_id"];
                        float x = json["x"];
                        float y = json["y"];
                        float dx = json["dx"];
                        float dy = json["dy"];

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

                            execute_params(update_sql, x, y, dx, dy);
                            
                            nlohmann::json  response_json;
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
                    } catch (const nlohmann::json::exception& e) {
                        nlohmann::json error_json;
                        error_json["success"] = false;
                        error_json["error"] = "Invalid Json format: " + std::string(e.what());
                        
                        m_response.result(http::status::bad_request);
                        m_response.set(http::field::content_type, "application/json");
                        m_response.body() = error_json.dump();

                        std::cerr << "JSON parsing error: " << e.what() << std::endl;
                    } catch (const pg_sql_exception& e) {
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
                } else {
                    m_response.result(http::status::not_found);
                    m_response.body() = "POST endpoint not found\r\n";
                }
            }

            void create_response() { 
                if (m_request.target() == "/hello") {
                    m_response.set(http::field::content_type, "text/plain");
                    m_response.body() = "Hello C++";
                } else if (m_request.target() == "/inf_qwq/get_all_rtsp_sources") {
                    try {
                        std::cout << "GET request to /inf_qwq/get_all_rtsp_sources" << std::endl;
                        
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
                    } catch (const pg_sql_exception& e) {
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
                }  else {
                    m_response.result(http::status::not_found);
                    m_response.set(http::field::content_type, "text/plain");
                    m_response.body() = "File not found\r\n" ;
                }
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
    }   // HTTP
}   // NAMESPACE INF_QWQ
#endif // HTTP_CONNECTION_H
