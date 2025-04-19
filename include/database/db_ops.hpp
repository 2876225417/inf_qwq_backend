#ifndef DB_OPS_HPP
#define DB_OPS_HPP

#include <database/db_conn.h>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <type_traits>

#ifdef USE_PGSQL
#include <pqxx/internal/statement_parameters.hxx>
#endif

#ifdef USE_MYSQL
#include <mysqlx/common/util.h>
#include <mysqlx/devapi/common.h>
#include <mysqlx/devapi/result.h>
#include <mysqlx/xdevapi.h>
#endif
 

namespace inf_qwq::database {
#ifdef USE_PGSQL
    inline int execute_non_query(const std::string& sql) {
        try {
            auto& conn = pg_connection::get_instance();
            if (!conn.is_initialized()) 
                throw database_exception(std::string("Database connection not initialized"));
            pqxx::work txn(conn.get_connection());
            pqxx::result result = txn.exec(sql);
            txn.commit();
            return result.affected_rows();
        } catch (const pqxx::sql_error& e) {
            throw database_exception("SQL error: " + std::string(e.what()) 
                                                    + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw database_exception(std::string("Exception error: " + std::string(e.what())));
        }
    }

    inline pqxx::result execute_query(const std::string& sql) {
        try {
            auto& conn = pg_connection::get_instance();
            if (!conn.is_initialized()) 
                throw database_exception(std::string("Database connection not initialized"));
            pqxx::work txn(conn.get_connection());
            pqxx::result result = txn.exec(sql);
            txn.commit();
            return result;
        } catch (const pqxx::sql_error& e) {
            throw database_exception( "SQL error: " + std::string(e.what()) 
                                                     + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw database_exception(std::string("Exception error: " + std::string(e.what())));
        }
    }

    template <typename... Args>
    inline pqxx::result execute_params( const std::string& sql
                                      , Args&&... args
                                      ) {
        try {
            auto& conn = pg_connection::get_instance();
            if (!conn.is_initialized()) 
                throw database_exception(std::string("Database connection not initialized"));
            pqxx::work txn(conn.get_connection());
            pqxx::result result = txn.exec_params(sql, std::forward<Args>(args)...);
            txn.commit();
            return result;
        } catch (const pqxx::sql_error& e) {
            throw database_exception("SQL error: " + std::string(e.what()) 
                                    + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw database_exception("Parameterized query error: " + std::string(e.what()));
        }
    }

    inline void
    execute_transaction(const std::function<void(pqxx::work&)>& transaction_func) {
        try {
            auto& conn = pg_connection::get_instance();
            if (!conn.is_initialized()) 
                throw database_exception(std::string("Database connection not initialized"));
            pqxx::work txn(conn.get_connection());
            transaction_func(txn);
            txn.commit();
        } catch (const pqxx::sql_error& e) {
            throw database_exception("Transaction SQL error: " + std::string(e.what()) 
                                    +  ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw database_exception("Transaction error: " + std::string(e.what()));
        }
    }

    template <typename Container>
    inline int
    batch_insert( const std::string& table
                , const std::vector<std::string>& columns
                , const Container& data
                ){
        try {
            auto& conn = pg_connection::get_instance();
            if (!conn.is_initialized())
                throw database_exception(std::string("Database connection not initialized"));
            pqxx::work txn(conn.get_connection());

            std::string column_list;
            for (size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) column_list += ", ";
                column_list += txn.quote_name(columns[i]);
            }
            
            std::string placeholders;
            for (size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) placeholders += ", ";
                placeholders += "$" + std::to_string(i + 1);
            }

            std::string insert_sql = "INSERT INTO " + txn.quote_name(table) 
                                   + " (" + column_list + ") VALUES (" + placeholders +")";
            
            int affected_rows = 0;
            for (const auto& row: data) {
                pqxx::result r = txn.exec_params(insert_sql, row);
                affected_rows += r.affected_rows();
            }
            txn.commit();
            return affected_rows;
        } catch (const pqxx::sql_error& e) {
            throw database_exception("Batch insert SQL error: " + std::string(e.what()) 
                                    +  ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw database_exception("Exception error: " + std::string(e.what()));
        }
    }       

    inline bool table_exists(const std::string& table_name) {
        try {
            auto& conn = pg_connection::get_instance();
            if (!conn.is_initialized()) 
                throw database_exception(std::string("Database connection not initialized"));
            pqxx::work txn{conn.get_connection()};
            pqxx::result result = txn.exec(
                "SELECT EXISTS (SELECT 1 FROM information_schema.tables) "
                "WHERE table_schema = 'public' AND table_name = " + txn.quote(table_name) + ")"
            );
            txn.commit();
            return result[0][0].as<bool>();
        } catch (const std::exception& e) {
            throw database_exception("Error checking table existence: " + std::string(e.what()));
        }
    }

    template <typename T>
    inline T get_scalar(const std::string& sql) {
        try {
            auto& conn = pg_connection::get_instance();
            if (!conn.is_initialized()) 
                throw database_exception(std::string("Database connection not initialized"));
            
            pqxx::work txn{conn.get_connection()};
            pqxx::row row = txn.exec1(sql);
            txn.commit();
            return row[0].as<T>();
        } catch (const pqxx::sql_error& e) {
            throw database_exception("SQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw database_exception("Error getting scalar value: " + std::string(e.what()));
        }
    }
#endif

#ifdef USE_MYSQL
inline int execute_non_query(const std::string& sql) {
    try {
        auto& conn = mysql_connection::get_instance();
        if (!conn.is_initialized()) 
            throw database_exception(std::string("Database connection not initialized"));

        auto result = conn.get_connection().sql(sql).execute();
        return static_cast<int>(result.getAffectedItemsCount());
    } catch (const mysqlx::Error& e) {
        throw database_exception("MySQL Error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw database_exception("Exception error: " + std::string(e.what()));
    }
}

inline mysqlx::RowResult execute_query(const std::string& sql) {
    try {
        auto& conn = mysql_connection::get_instance();
        if (!conn.is_initialized()) 
            throw database_exception(std::string("Database connection not initialized"));
        return conn.get_connection().sql(sql).execute();
    } catch (const mysqlx::Error& e) {
        throw database_exception("MySQL error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw database_exception("Exception error: " + std::string(e.what()));
    }
}

inline void bind_params(mysqlx::SqlStatement& stmt) {}
template <typename T, typename... Args>
inline void bind_params(mysqlx::SqlStatement& stmt, T&& value, Args&&... args) {
    stmt.bind(std::forward<T>(value));
    if constexpr (sizeof...(args) > 0)
        bind_params(stmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline mysqlx::RowResult execute_params(const std::string& sql, Args&&... args) {
    try {
        auto& conn = mysql_connection::get_instance();
        if (!conn.is_initialized()) 
            throw database_exception(std::string("Database connection not initialized"));    
        
        std::string prepared_sql = sql;
        size_t param_count = sizeof...(Args);
        
        mysqlx::SqlStatement stmt = conn.get_connection().sql(prepared_sql);

        if constexpr (sizeof...(args) > 0) bind_params(stmt, std::forward<Args>(args)...);

        return stmt.execute();
    } catch (const mysqlx::Error& e) {
        throw database_exception("MySQL error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw database_exception("Exception error: " + std::string(e.what()));
    }
}

inline void execute_transaction(const std::function<void(mysqlx::Session&)>& transaction_func) {
    try {
        auto& conn = mysql_connection::get_instance();
        if (!conn.is_initialized())
            throw database_exception(std::string("Database connection not initialized"));

        auto& session = conn.get_connection();
        session.startTransaction();

        try {
            transaction_func(session);
            session.commit();
        } catch (...) {
            session.rollback();
            throw;
        }
    } catch (const mysqlx::Error& e) {
        throw database_exception("MySQL error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw database_exception("Exception error: " + std::string(e.what()));
    }
}

template <typename Container>
inline int batch_insert( const std::string& table
                       , const std::vector<std::string>& columns
                       , const Container& data) {
    try {
        auto& conn = mysql_connection::get_instance();
        if (!conn.is_initialized())
            throw database_exception(std::string("Database connection not initialized"));

        auto& session = conn.get_connection();
        session.startTransaction();

        std::string column_list;
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) column_list += ", ";
            column_list += "`" + columns[i] + "`";
        }

        std::string placeholders;
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) placeholders += ", ";
            placeholders += "?";
        }

        std::string insert_sql = "INSERT INTO `" + table 
                               + "` (" + column_list + ") VALUES (" + placeholders + ")";
        int affected_rows = 0;

        try {
            for (const auto& row: data) {
                mysqlx::SqlStatement stmt = session.sql(insert_sql);

                for (const auto& value: row) stmt.bind(value);

                auto result = stmt.execute();
                affected_rows += static_cast<int>(result.getAffectedItemsCount());
            }
            
            session.commit();
            return affected_rows;
        } catch (...) {
            session.rollback();
            throw;
        }
        
    } catch (const mysqlx::Error& e) {
        throw database_exception("MySQL error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw database_exception("Exception error: " + std::string(e.what()));
    }
}

inline bool table_exists(const std::string& table_name) {
    try {
        auto& conn = mysql_connection::get_instance();
        if (!conn.is_initialized()) 
            throw database_exception(std::string("Database connection not intialized"));
        
        auto result = conn.get_connection().sql(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = DATABASE() AND table_name = ?"
        ).bind(table_name)
         .execute();

        auto row = result.fetchOne();
        return row[0].get<int>() > 0;
    } catch (const std::exception& e) {
        throw database_exception("Error checking table existence: " + std::string(e.what()));
    }
}

template <typename T>
inline T get_scalar(const std::string& sql) {
    try {
        auto& conn = mysql_connection::get_instance();
        if (!conn.is_initialized())
            throw database_exception(std::string("Database connection not initialzied"));
        
        auto result = conn.get_connection().sql(sql).execute();
        auto row = result.fetchOne();
        
        if (!row)
            throw database_exception(std::string("Query did not returned any rows"));
        
        return row[0].get<T>();
    } catch (const mysqlx::Error& e) {
        throw database_exception("MySQL error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw database_exception("Exception error: " + std::string(e.what()));
    }
}


template <typename T>
inline std::vector<T> r2vector(mysqlx::RowResult& result, int column_index = 0) {
    std::vector<T> values;
    for (auto row: result)
        values.push_back(row[column_index].get<T>());
    return values;
}

template <typename T>
inline std::vector<std::vector<T>> r2matrix(mysqlx::RowResult& result) {
    std::vector<std::vector<T>> matrix;
    for (auto row: result) {
        std::vector<T> row_values;
        for (unsigned i = 0; i < row.colCount(); ++i) 
            row_values.push_back(row[i].get<T>());
        matrix.push_back(row_values);
    }
    return matrix;
}
#endif
}

#endif // DB_OPS_HPP
