

#ifndef HTTP_CONNECTION
#define HTTP_CONNECTION

#include <boost/beast/version.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>


namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;

using tcp = boost::asio::ip::tcp;


class http_connection
    : public std::enable_shared_from_this<http_connection> {
    
    http_connection(tcp::socket socket) 


};



#endif // HTTP_SERVER
