#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class http_connection : public std::enable_shared_from_this<http_connection>
{
public:
    http_connection(tcp::socket socket)
        : socket_(std::move(socket))
    {
    }

    void start()
    {
        read_request();
    }

private:
    tcp::socket socket_;
    beast::flat_buffer buffer_{8192};
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;

    void read_request()
    {
        auto self = shared_from_this();

        http::async_read(
            socket_,
            buffer_,
            request_,
            [self](beast::error_code ec, std::size_t bytes_transferred)
            {
                boost::ignore_unused(bytes_transferred);
                if(!ec)
                    self->process_request();
            });
    }

    void process_request()
    {
        response_.version(request_.version());
        response_.keep_alive(false);

        switch(request_.method())
        {
        case http::verb::get:
            response_.result(http::status::ok);
            response_.set(http::field::server, "Beast");
            create_response();
            break;
        default:
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "text/plain");
            response_.body() = "Invalid request-method '" + std::string(request_.method_string()) + "'";
            break;
        }

        write_response();
    }

    void create_response()
    {
        if(request_.target() == "/hello")
        {
            response_.set(http::field::content_type, "text/plain");
            response_.body() = "Hello C++";
        }
        else
        {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "text/plain");
            response_.body() = "File not found\r\n";
        }
    }

    void write_response()
    {
        auto self = shared_from_this();

        response_.content_length(response_.body().size());

        http::async_write(
            socket_,
            response_,
            [self](beast::error_code ec, std::size_t)
            {
                self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            });
    }
};

class http_server
{
public:
    http_server(net::io_context& ioc, tcp::endpoint endpoint)
        : ioc_(ioc)
        , acceptor_(ioc)
    {
        beast::error_code ec;

        acceptor_.open(endpoint.protocol(), ec);
        if(ec) { fail(ec, "open"); return; }

        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if(ec) { fail(ec, "set_option"); return; }

        acceptor_.bind(endpoint, ec);
        if(ec) { fail(ec, "bind"); return; }

        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if(ec) { fail(ec, "listen"); return; }
    }

    void run()
    {
        do_accept();
    }

private:
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

    void do_accept()
    {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket)
            {
                if(!ec)
                    std::make_shared<http_connection>(std::move(socket))->start();
                do_accept();
            });
    }

    void fail(beast::error_code ec, char const* what)
    {
        std::cerr << what << ": " << ec.message() << "\n";
    }
};

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 3)
        {
            std::cerr << "Usage: http-server-sync <address> <port>\n";
            std::cerr << "Example:\n";
            std::cerr << "    http-server-sync 0.0.0.0 8080\n";
            return EXIT_FAILURE;
        }

        auto const address = net::ip::make_address(argv[1]);
        unsigned short port = static_cast<unsigned short>(std::atoi(argv[2]));

        net::io_context ioc{1};

        http_server server{ioc, {address, port}};
        server.run();

        ioc.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

