


#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/beast/core/error.hpp>
#include <http/http_connection.h>
#include <memory>
#include <iostream>

namespace inf_qwq {
    namespace http {
        class http_server {
        public: 
            explicit http_server( net::io_context& io_ctx
                                , tcp::endpoint endpoint
                                ):m_io_ctx{io_ctx}
                                , m_acceptor(io_ctx)
                                {
                beast::error_code ec;
                
                m_acceptor.open(endpoint.protocol(), ec);
                if (ec) { fail(ec, "open"); return; }

                m_acceptor.set_option(net::socket_base::reuse_address(true), ec);
                if (ec) { fail(ec, "set_option"); return; }

                m_acceptor.bind(endpoint, ec);
                if (ec) { fail(ec, "bind"); return; }

                m_acceptor.listen(net::socket_base::max_listen_connections, ec);
                if (ec) { fail(ec, "listen"); return; } 
            }

            void run() { do_accept(); }

        private:
            net::io_context&    m_io_ctx;
            tcp::acceptor       m_acceptor;

            void do_accept() {
                m_acceptor.async_accept([this]( beast::error_code ec
                                              , tcp::socket socket) {
                                            if (!ec) 
                                                std::make_shared<http_connection>(std::move(socket))->start();
                                            do_accept();
                                        });
            }

            void fail(beast::error_code ec, const char* what) {
                std::cerr << what << ": " << ec.message() << "\n";
            }
        };
    }   // NAMESPACE HTTP
}   // NAMESPACE INF_QWQ

#endif // HTTP_SERVER_H
