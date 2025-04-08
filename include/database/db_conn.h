

#ifndef DB_CONN_H
#define DB_CONN_H

#include <mutex>
#include <pqxx/pqxx>
#include <functional>
#include <stdexcept>
#include <string>
#include <memory>

#include <iostream>

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
                    return "host="      + host 
                        + " port="      + std::to_string(port)
                        + " dbname="    + db_name
                        + " user="      + user 
                        + " password="  + password;
                }
                
                bool is_valid() const { return !db_name.empty() && !user.empty(); }
            };
            
            class pg_sql_exception: public std::runtime_error {
            public: 
                explicit pg_sql_exception(const std::string& msg)
                    : std::runtime_error{msg} {}
            };

            class pg_sql_conn {
            public:
                // Not allowed
                pg_sql_conn(const pg_sql_conn&) = delete;
                pg_sql_conn& operator=(const pg_sql_conn&) = delete;

                pg_sql_conn(pg_sql_conn&& other) = delete;
                pg_sql_conn& operator=(pg_sql_conn&& other) noexcept  = default;
            
                static pg_sql_conn& get_instance(const conn_config& config = {}) {
                    static std::mutex mutex;
                    std::lock_guard<std::mutex> lock(mutex);
                    
                    static pg_sql_conn instance(config);

                    if ( config.conn_string() != instance.m_config.conn_string() &&
                         config.is_valid()
                       ) {
                        instance.m_config = config;
                        instance.reconnect();
                    }
                    
                    if (!instance.is_initialized() && !config.is_valid())
                        throw pg_sql_exception("Database connection not initialized and no valid configurration");
                    
                    return instance;
                }

                bool is_initialized() const { return m_conn && m_conn->is_open(); }

                void reconnect() {
                    if (!m_config.is_valid()) 
                        throw pg_sql_exception("Cannot reconnect with invalid configuration");

                    try {
                        m_conn = std::make_unique<pqxx::connection>(m_config.conn_string());
                        if (!m_conn->is_open()) 
                            throw pg_sql_exception("Failed to reopen database connection");
                        } catch (const pqxx::sql_error& e) {
                            throw pg_sql_exception("SQL error during reconnection: " + std::string(e.what()) + ", Query: " + e.query());    
                        } catch (const std::exception& e) {
                            throw pg_sql_exception("Reconnection error: " + std::string(e.what()));
                        }
                }

                pqxx::connection& get_conn() {
                    if (!m_conn) throw pg_sql_exception("Connection not initialized");
                    if (!m_conn->is_open()) reconnect();
                    return *m_conn;
                } 

                const conn_config& get_config() const { return m_config; }

                
            private:
                explicit pg_sql_conn(const conn_config& config)
                    : m_config{config} 
                    {
                    if (!config.db_name.empty()) {
                        try {
                            m_conn = std::make_unique<pqxx::connection>(config.conn_string());
                            if (!m_conn->is_open()) 
                                throw pg_sql_exception("Failed to open database connection");
                        } catch (const pqxx::sql_error& e) {
                            throw pg_sql_exception("SQL error: " + std::string(e.what()) + ", Query: " + e.query());
                        } catch (const std::exception& e) {
                            throw pg_sql_exception("Connection error: " + std::string(e.what()));
                        }
                    }
                }

                ~pg_sql_conn() = default;

                conn_config m_config;
                std::unique_ptr<pqxx::connection> m_conn;
            };
        } // namespace pg_sql
    } // namespace database
} // namespace inf_qwq


#endif // DB_CONN_H
