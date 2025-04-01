#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/file_body_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/core/ignore_unused.hpp>



#include <iostream>



namespace inf_qwq {
    namespace http {
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

            void create_response() { 
                if (m_request.target() == "/hello") {
                    m_response.set(http::field::content_type, "text/plain");
                    m_response.body() = "Hello C++";
                } else {
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
