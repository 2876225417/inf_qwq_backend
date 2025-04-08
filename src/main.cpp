
/* INF_QWQ_BACKEND @CHUNHUI
 * @ Edited by ppQwQqq 
 * @ Powered by Boost
 * @ ChunHui Info
 *
 *
 * */



#include "database/db_ops.hpp"
#include <cstdlib>
#include <http/http_connection.h>
#include <http/http_server.h>

#include <database/db_conn.h>
#include <iostream>



int main(int argc, char* argv[])
{
    using namespace inf_qwq::database::pg_sql;

    conn_config config;
    config.host = "localhost";
    config.port = 5432;
    config.db_name = "inf_qwq";
    config.user = "ppqwqqq";
    config.password = "20041025";

    //pg_sql_conn conn(config);
    
    auto& db = pg_sql_conn::get_instance(config);


    if (!db.is_initialized()) {
        std::cerr << "Failed to initialize database connection" << std::endl;
        return EXIT_FAILURE;
    }

    if (!table_exists("users")) {
        execute_non_query( 
            "CREATE TABLE users ("
            "id SERIAL PRIMARY KEY, "
            "name VARCHAR(100) NOT NULL, "
            "email VARCHAR(100) UNIQUE NOT NULL)"
                          );

        std::cout << "Created users table " << std::endl;
    }

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

