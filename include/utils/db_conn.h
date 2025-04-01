

#ifndef DB_CONN_H
#define DB_CONN_H

#include <pqxx/pqxx>
#include <functional>
#include <stdexcept>
#include <string>
#include <memory>

namespace inf_qwq {
    namespace database {
        namespace pg_sql {
            struct conn_config {
                std::string host = "localhost";
                int port = 5432;
                std::string db_name;
                std::string user;
                std::string password;
            
                std::string conn_string() const {
                    return "host=" + host 
                        + " port=" + std::to_string(port)
                        + " dbname=" + db_name
                        + " user=" + user 
                        + " password" + password;
                }
            };
            class pg_sql_conn {};
        } // namespace pg_sql
    } // namespace database
} // namespace inf_qwq


#endif // DB_CONN_H
