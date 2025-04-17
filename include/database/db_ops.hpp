#ifndef DB_OPS_HPP
#define DB_OPS_HPP

#include <database/db_conn.h>
#include <exception>
#include <string_view>
#include <vector>
#include <functional>
#include <type_traits>

#ifdef USE_PGSQL
#include <pqxx/internal/statement_parameters.hxx>
#endif

namespace inf_qwq::database {

// Forward declarations of database-specific result types
#ifdef USE_PGSQL
using PgResult = pqxx::result;
#endif

#ifdef USE_MYSQL
using MySqlResult = mysqlx::SqlResult;
#endif

// Generic result wrapper to handle different database result types
template<Db_Type T>
class QueryResult;

#ifdef USE_PGSQL
// PostgreSQL specialization for QueryResult
template<>
class QueryResult<DbType::PostgreSQL> {
public:
    explicit QueryResult(pqxx::result result) : m_result(std::move(result)) {}

    [[nodiscard]] size_t size() const { return m_result.size(); }
    [[nodiscard]] size_t affected_rows() const { return m_result.affected_rows(); }
    [[nodiscard]] bool empty() const { return m_result.empty(); }
    
    // Access row by index
    pqxx::row operator[](size_t idx) const { return m_result[idx]; }
    
    // Iterators for range-based for loops
    auto begin() const { return m_result.begin(); }
    auto end() const { return m_result.end(); }
    
    // Get underlying result
    [[nodiscard]] const pqxx::result& get_result() const { return m_result; }

private:
    pqxx::result m_result;
};
#endif

#ifdef USE_MYSQL
// MySQL specialization for QueryResult
template<>
class QueryResult<Db_Type::MySQL> {
public:
    explicit QueryResult(mysqlx::SqlResult result) : m_result(std::move(result)) {}

    [[nodiscard]] size_t size() const { 
        // MySQL X DevAPI doesn't directly provide size, so we count rows
        size_t count = 0;
        auto rows = m_result.fetchAll();
        for (const auto& row : rows) {
            count++;
        }
        return count;
    }
    
    [[nodiscard]] size_t affected_rows() const { 
        return m_result.getAffectedItemsCount(); 
    }
    
    [[nodiscard]] bool empty() const { 
        return m_result.count() == 0; 
    }
    
    // Access row by index (note: this is inefficient for MySQL as it fetches all rows)
    mysqlx::Row operator[](size_t idx) const { 
        auto rows = m_result.fetchAll();
        if (idx >= rows.size()) {
            throw DatabaseException("Row index out of bounds");
        }
        return rows[idx];
    }
    
    // Get underlying result
    [[nodiscard]] const mysqlx::SqlResult& get_result() const { return m_result; }

private:
    mysqlx::SqlResult m_result;
};
#endif

// Database operations template class
template<DbType T>
class DatabaseOperations {
public:
    using ConnectionManagerType = ConnectionManager<T>;
    using ResultType = QueryResult<T>;
    
    // Execute non-query SQL statement
    static int execute_non_query(std::string_view sql);
    
    // Execute query and return results
    static ResultType execute_query(std::string_view sql);
    
    // Execute parameterized query
    template <typename... Args>
    static ResultType execute_params(std::string_view sql, Args&&... args);
    
    // Execute transaction with custom function
    template <typename Func>
    static void execute_transaction(Func&& transaction_func);
    
    // Batch insert records
    template <typename Container>
    static int batch_insert(
        std::string_view table,
        const std::vector<std::string>& columns,
        const Container& data
    );
    
    // Check if table exists
    static bool table_exists(std::string_view table_name);
    
    // Get scalar value from query
    template <typename T>
    static T get_scalar(std::string_view sql);
};

#ifdef USE_PGSQL
// PostgreSQL specialization
template<>
class DatabaseOperations<DbType::PostgreSQL> {
public:
    using ConnectionManagerType = ConnectionManager<DbType::PostgreSQL>;
    using ResultType = QueryResult<DbType::PostgreSQL>;

    static int execute_non_query(std::string_view sql) {
        try {
            auto& conn = ConnectionManagerType::get_instance();      
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
                
            pqxx::work txn(conn.get_connection());
            pqxx::result result = txn.exec(std::string(sql));
            txn.commit();
            return result.affected_rows();
        } catch (const pqxx::sql_error& e) {
            throw DatabaseException("SQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw DatabaseException("Execution error: " + std::string(e.what()));
        }
    }

    static ResultType execute_query(std::string_view sql) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
                
            pqxx::work txn(conn.get_connection());
            pqxx::result result = txn.exec(std::string(sql));
            txn.commit();
            return ResultType(result);
        } catch (const pqxx::sql_error& e) {
            throw DatabaseException("SQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw DatabaseException("Execution error: " + std::string(e.what()));
        }
    }

    template <typename... Args>
    static ResultType execute_params(std::string_view sql, Args&&... args) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");

            pqxx::work txn(conn.get_connection());
            pqxx::result result = txn.exec_params(std::string(sql), std::forward<Args>(args)...);
            txn.commit();
            return ResultType(result);
        } catch (const pqxx::sql_error& e) {
            throw DatabaseException("SQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw DatabaseException("Parameterized query error: " + std::string(e.what()));
        }
    }

    static void execute_transaction(const std::function<void(pqxx::work&)>& transaction_func) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
        
            pqxx::work txn(conn.get_connection());
            transaction_func(txn);
            txn.commit();
        } catch (const pqxx::sql_error& e) {
            throw DatabaseException("Transaction SQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw DatabaseException("Transaction error: " + std::string(e.what()));
        }
    }
    
    template <typename Container>
    static int batch_insert(
        std::string_view table,
        const std::vector<std::string>& columns,
        const Container& data
    ) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");

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
            
            std::string insert_sql = "INSERT INTO " + txn.quote_name(std::string(table))
                                   + " (" + column_list +") VALUES (" + placeholders + ")";
            int affected_rows = 0;
            
            for (const auto& row: data) {
                pqxx::result r = txn.exec_params(insert_sql, row);
                affected_rows += r.affected_rows(); 
            }

            txn.commit();
            return affected_rows;
        } catch (const pqxx::sql_error& e) {
            throw DatabaseException("Batch insert SQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw DatabaseException("Batch insert error: " + std::string(e.what()));
        }
    }

    static bool table_exists(std::string_view table_name) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
        
            pqxx::work txn{conn.get_connection()};
            pqxx::result result = txn.exec(
                "SELECT EXISTS (SELECT 1 FROM information_schema.tables "
                "WHERE table_schema = 'public' AND table_name = " 
                + txn.quote(std::string(table_name)) + ")");
            txn.commit();
            return result[0][0].as<bool>();
        } catch (const std::exception& e) {
            throw DatabaseException("Error checking table existence: " + std::string(e.what()));
        }
    }

    template <typename T>
    static T get_scalar(std::string_view sql) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
        
            pqxx::work txn(conn.get_connection());
            pqxx::row row = txn.exec1(std::string(sql));
            txn.commit();
            return row[0].as<T>();
        } catch (const pqxx::sql_error& e) {
            throw DatabaseException("SQL error: " + std::string(e.what()) + ", Query: " + e.query());
        } catch (const std::exception& e) {
            throw DatabaseException("Error getting scalar value: " + std::string(e.what()));
        }
    }
};
#endif

#ifdef USE_MYSQL
// MySQL specialization
template<>
class DatabaseOperations<DbType::MySQL> {
public:
    using ConnectionManagerType = ConnectionManager<DbType::MySQL>;
    using ResultType = QueryResult<DbType::MySQL>;

    static int execute_non_query(std::string_view sql) {
        try {
            auto& conn = ConnectionManagerType::get_instance();      
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
                
            mysqlx::SqlResult result = conn.get_connection().sql(std::string(sql)).execute();
            return static_cast<int>(result.getAffectedItemsCount());
        } catch (const mysqlx::Error& e) {
            throw DatabaseException("MySQL error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw DatabaseException("Execution error: " + std::string(e.what()));
        }
    }

    static ResultType execute_query(std::string_view sql) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
                
            mysqlx::SqlResult result = conn.get_connection().sql(std::string(sql)).execute();
            return ResultType(result);
        } catch (const mysqlx::Error& e) {
            throw DatabaseException("MySQL error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw DatabaseException("Execution error: " + std::string(e.what()));
        }
    }

    // MySQL parameter binding requires different approach
    template <typename... Args>
    static ResultType execute_params(std::string_view sql_template, Args&&... args) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");

            // MySQL X DevAPI uses ? placeholders
            std::string sql_str(sql_template);
            mysqlx::SqlStatement stmt = conn.get_connection().sql(sql_str);
            
            // Bind parameters (fold expression for parameter pack)
            int dummy[] = { (stmt.bind(std::forward<Args>(args)), 0)... };
            (void)dummy; // Suppress unused variable warning
            
            mysqlx::SqlResult result = stmt.execute();
            return ResultType(result);
        } catch (const mysqlx::Error& e) {
            throw DatabaseException("MySQL error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw DatabaseException("Parameterized query error: " + std::string(e.what()));
        }
    }

    static void execute_transaction(const std::function<void(mysqlx::Session&)>& transaction_func) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
        
            mysqlx::Session& session = conn.get_connection();
            session.startTransaction();
            
            try {
                transaction_func(session);
                session.commit();
            } catch (...) {
                session.rollback();
                throw;
            }
        } catch (const mysqlx::Error& e) {
            throw DatabaseException("Transaction MySQL error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw DatabaseException("Transaction error: " + std::string(e.what()));
        }
    }
    
    template <typename Container>
    static int batch_insert(
        std::string_view table,
        const std::vector<std::string>& columns,
        const Container& data
    ) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");

            mysqlx::Session& session = conn.get_connection();
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
            
            std::string insert_sql = "INSERT INTO `" + std::string(table) + "`"
                                   + " (" + column_list +") VALUES (" + placeholders + ")";
            
            int affected_rows = 0;
            
            try {
                for (const auto& row: data) {
                    mysqlx::SqlStatement stmt = session.sql(insert_sql);
                    
                    // Bind each value in the row
                    size_t i = 0;
                    for (const auto& value : row) {
                        stmt.bind(value);
                        i++;
                    }
                    
                    mysqlx::SqlResult result = stmt.execute();
                    affected_rows += static_cast<int>(result.getAffectedItemsCount());
                }
                
                session.commit();
                return affected_rows;
            } catch (...) {
                session.rollback();
                throw;
            }
        } catch (const mysqlx::Error& e) {
            throw DatabaseException("Batch insert MySQL error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw DatabaseException("Batch insert error: " + std::string(e.what()));
        }
    }

    static bool table_exists(std::string_view table_name) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
        
            std::string query = "SELECT COUNT(*) FROM information_schema.tables "
                               "WHERE table_schema = DATABASE() AND table_name = ?";
            
            mysqlx::SqlStatement stmt = conn.get_connection().sql(query);
            stmt.bind(std::string(table_name));
            
            mysqlx::SqlResult result = stmt.execute();
            mysqlx::Row row = result.fetchOne();
            return row[0].get<int>() > 0;
        } catch (const std::exception& e) {
            throw DatabaseException("Error checking table existence: " + std::string(e.what()));
        }
    }

    template <typename T>
    static T get_scalar(std::string_view sql) {
        try {
            auto& conn = ConnectionManagerType::get_instance();
            if (!conn.is_initialized()) 
                throw DatabaseException("Database connection not initialized");
        
            mysqlx::SqlResult result = conn.get_connection().sql(std::string(sql)).execute();
            mysqlx::Row row = result.fetchOne();
            
            if (row.isNull()) {
                throw DatabaseException("Query returned no rows");
            }
            
            return row[0].get<T>();
        } catch (const mysqlx::Error& e) {
            throw DatabaseException("MySQL error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw DatabaseException("Error getting scalar value: " + std::string(e.what()));
        }
    }
};
#endif

// Type aliases for convenience
#ifdef USE_PGSQL
using PgOperations = DatabaseOperations<DbType::PostgreSQL>;
#endif

#ifdef USE_MYSQL
using MySqlOperations = DatabaseOperations<DbType::MySQL>;
#endif

} // namespace inf_qwq::database

#endif // DB_OPS_HPP
