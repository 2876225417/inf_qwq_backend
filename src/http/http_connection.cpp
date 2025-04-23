

#ifndef HTTP_IMPL_HPP
#define HTTP_IMPL_HPP

//#include <http/http_connection.hpp>
#include <unordered_map>
#include <boost/beast/http.hpp>
#include <http/http_connection.h>

namespace inf_qwq::http {

class http_connection;


enum class http_method {
    GET,
    POST,
    PUT,
    DELETE,
    UNKNOWN
};

inline http_method enum2method(http::verb method) {
    switch (method) {
        case http::verb::get: return http_method::GET;
        case http::verb::post: return http_method::POST;
        case http::verb::put: return http_method::PUT;
        case http::verb::delete_: return http_method::DELETE;
        default: return http_method::UNKNOWN;
    }
}


    
// void handle_hello(http_connection& conn);
// void handle_health_check(http_connection& conn);
// void handle_get_all_rtsp_sources(http_connection& conn);
// void handle_add_rtsp_source(http_connection& conn);
// void handle_update_cropped_coords(http_connection& conn);
// void handle_update_inf_result(http_connection& conn);
//void handle_not_found(http_connection& conn);


//const std::unordered_map<api_route, route_handler_func>& get_route_handlers();


}
#endif // HTTP_IMPL_HPP
