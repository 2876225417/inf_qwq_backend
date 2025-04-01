
/* INF_QWQ_BACKEND @CHUNHUI
 * @ Edited by ppQwQqq 
 * @ Powered by Boost
 * @ ChunHui Info
 *
 *
 * */



#include <http/http_connection.h>
#include <http/http_server.h>



int main(int argc, char* argv[])
{
    using namespace inf_qwq::http;
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

