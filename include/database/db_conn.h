#ifndef DB_CONN_H
#define DB_CONN_H


#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <memory>
#include <iostream>
#include <optional>
#include <mutex>
#include <utility>
#include <variant>
#include <type_traits>
#include <string_view>
#ifdef USE_PGSQL
#include <pqxx/pqxx>
#endif

#ifdef USE_MYSQL
#include <mysqlx/xdevapi.h>
#include <mysqlx/devapi/common.h>
#include <mysqlx/devapi/settings.h>
#endif

namespace inf_qwq::database {

enum class Db_Type { PostgreSQL, MySQL };

class connection_config {
public:
    connection_config() = default;
    
    connection_config( std::string_view host
                     , int port
                     , std::string_view db_name
                     , std::string_view user
                     , std::string_view password
                     ):m_host{host}
                     , m_port{port}
                     , m_db_name{db_name}
                     , m_user{user}
                     , m_password{password} {}
    
    connection_config& host(std::string_view host) {
        m_host = host;
        return *this;
    }

    connection_config& port(int port) {
        m_port = port;
        return *this;
    }

    connection_config& db_name(std::string_view db_name) {
        m_db_name = db_name;
        return *this;
    }

    connection_config& user(std::string_view user) {
        m_user = user;
        return *this;
    }

    connection_config& password(std::string_view password) {
        m_password = password;
        return *this;
    }

    const std::string& host()     const { return m_host;     }
    /* */ int/*     */ port()     const { return m_port;     }
    const std::string& db_name()  const { return m_db_name;  }
    const std::string& user()     const { return m_user;     }
    const std::string& password() const { return m_password; }
    
    std::string conn_string() const {
        return "host="      + m_host
             + " port="     + std::to_string(m_port)
             + " dbname="   + m_db_name
             + " user="     + m_user
             + " password=" + m_password;
    }

    bool is_valid() const { return !m_db_name.empty() && !m_user.empty(); }

    bool operator==(const connection_config& other) const {
        return m_host       == other.m_host    && 
               m_port       == other.m_port    &&
               m_db_name    == other.m_db_name &&
               m_user       == other.m_user    && 
               m_password   == other.m_password; 
    }

    bool operator!=(const connection_config& other) const {
        return !(*this == other);
    }
private:
    std::string m_host = "localhost";
    int m_port = 0;
    std::string m_db_name;
    std::string m_user;
    std::string m_password;
};

class database_exception: public std::runtime_error {
public:
    explicit database_exception(const std::string& msg) 
        : std::runtime_error{msg} {}

    explicit database_exception(std::string_view msg)
        : std::runtime_error{std::string{msg}} {}
};

template <Db_Type T> struct db_connection_traits{};
template <Db_Type T> class connection_manager;

#ifdef USE_PGSQL
template <>
struct db_connection_traits<Db_Type::PostgreSQL> {
    using connection_type = pqxx::connection;
    using exception_type  = database_exception;
    static constexpr int default_port = 5432;

    static std::unique_ptr<pqxx::connection> 
    create_connection(const connection_config& config) {
        try {
            return std::make_unique<pqxx::connection>(config.conn_string());
        } catch (const pqxx::sql_error& e) {
            throw exception_type("PostgreSQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw exception_type("PostgreSQL connection error: " + std::string(e.what()));
        }
    }

    static bool is_connection_valid(pqxx::connection& conn) { return conn.is_open(); }

    static void test_connection(pqxx::connection& conn) {
        try {
            pqxx::work txn(conn);
            txn.exec1("SELECT 1");
            txn.commit();
        } catch (const std::exception& e) {
            throw exception_type("PostgreSQL connection test failed: " + std::string(e.what()));
        }
    }
};
#endif

#ifdef USE_MYSQL
template <>
struct db_connection_traits<Db_Type::MySQL> {
    using connection_type = mysqlx::Session;
    using exception_type  = database_exception;
    static constexpr int default_port = 33060;

    static std::unique_ptr<mysqlx::Session> 
    create_connection(const connection_config& config) {
        try {
            int port = config.port() == 0 ? default_port : config.port();

            mysqlx::SessionSettings settings( config.host() 
                                            , port
                                            , config.user()
                                            , config.password()
                                            , config.db_name()
                                            );
            return std::make_unique<mysqlx::Session>(settings);
        } catch (const mysqlx::Error& e) {
            throw exception_type("MySQL X DevAPI error: " + std::string(e.what()));
        } catch (const std::exception& e) {
           throw exception_type("MySQL connection error: " + std::string(e.what()));
        }
    }

    static bool
    is_connection_valid(mysqlx::Session& session) {
        try {
            test_connection(session);
            return true;
        } catch (...) { return false; }
    }

    static void
    test_connection(mysqlx::Session& session) {
        try { session.sql("SELECT 1").execute(); }
        catch (const std::exception& e) {
            throw exception_type("MySQL connection test failed: " + std::string(e.what()));
        }
    }
};
#endif 

template <Db_Type T>
class connection_manager {
public:
    using traits = db_connection_traits<T>;
    using connection_type = typename traits::connection_type;
    using exception_type = typename traits::exception_type;

    connection_manager(const connection_manager&) = delete;
    connection_manager& operator=(const connection_manager&) = delete;

    connection_manager(connection_manager&&) = delete;
    connection_manager& operator=(connection_manager&&) = delete;

    static connection_manager& 
    get_instance(const connection_config& config = {}) {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);

        static connection_manager instance(config);
        
        if (config != instance.m_config && config.is_valid()) {
            instance.m_config = config;
            instance.reconnect();
        } 

        if (!instance.is_initialized() && !config.is_valid()) {
            throw exception_type(std::string("Database connection not is_initialized and no valid configuration provided"));
        }
        return instance;
    }

    [[nodiscard]] bool 
    is_initialized() const {
        return m_connection && traits::is_connection_valid(*m_connection);
    }

    void reconnect() {
        if (!m_config.is_valid()) {
            throw exception_type(std::string("Cannot reconnect with invalid configuration"));
        }
        m_connection = traits::create_connection(m_config);

        if (!traits::is_connection_valid(*m_connection)) {
            throw exception_type(std::string("Failed to establish database connection"));
        }

        traits::test_connection(*m_connection);
    }

    connection_type& get_connection() {
        if (!m_connection) throw exception_type(std::string("Connection not initialized"));
        if (!traits::is_connection_valid(*m_connection)) reconnect();

        return *m_connection;
    }

    [[nodiscard]] const connection_config&
    get_config() const { return m_config; }

private:
    explicit connection_manager(const connection_config& config) 
        : m_config{config} 
        {
        if (config.is_valid()) {
            if (m_config.port() == 0) {
                const_cast<connection_config&>(m_config) = connection_config( m_config.host()
                                                                            , traits::default_port
                                                                            , m_config.db_name()
                                                                            , m_config.user()
                                                                            , m_config.password()
                                                                            );
            }
            reconnect();
        }
        
    }

    connection_config m_config;
    std::unique_ptr<connection_type> m_connection;
};

#ifdef USE_PGSQL
using pg_connection = connection_manager<Db_Type::PostgreSQL>;
#endif

#ifdef USE_MYSQL
using mysql_connection = connection_manager<Db_Type::MySQL>;
#endif

} // namespace inf_qwq


#endif // DB_CONN_H
