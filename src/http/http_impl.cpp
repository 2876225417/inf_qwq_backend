


// #include <unordered_map>

// #include <http/http_connection.h>

// namespace inf_qwq::http {

// void handle_hello(http_connection& conn) {
//     auto& m_response = conn.response();
//     m_response.set(http::field::content_type, "text/plain");
//     m_response.body() = "Hello C++";
// }

// void handle_health_check(http_connection& conn) {
//     auto& m_response = conn.response();
//     try {
//         bool database_connected = false;
//         try {
//             #ifdef USE_MYSQL
//             auto result = execute_query("SELECT 1");
//             database_connected = (result.count() > 0);
//             #endif
//             #ifdef USE_PGSQL
//             pqxx::result result = execute_query("SELECT 1");
//             database_connected = !result.empty();
//             #endif
//         } catch (const std::exception& e) {
//             database_connected = false;
//         }

//         nlohmann::json response_json;
//         response_json["status"] = "ok";
//         response_json["timestamp"] = std::time(nullptr);
//         response_json["service"] = "rtsp-monitor-server";
//         response_json["database_connected"] = database_connected;

//         m_response.result(http::status::ok);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = response_json.dump();

//         std::cout << "Health check request processed" << std::endl;
//     } catch (const std::exception& e) {
//         nlohmann::json error_json;
//         error_json["status"] = "error";
//         error_json["error"] = "Error processing health check: " + std::string(e.what());
        
//         m_response.result(http::status::internal_server_error);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = error_json.dump();

//         std::cerr << "Error during health check: " << e.what();
//     }
// }

// void handle_get_all_rtsp_sources(http_connection &conn) {
//     auto& m_response = conn.response(); 
//     try {
//         std::cout << "GET request to /inf_qwq/get_all_rtsp_sources" << std::endl;
        
//         #ifdef USE_MYSQL
//         auto result = execute_query(
//             "SELECT rtsp_id, rtsp_type, rtsp_username, rtsp_id, rtsp_port, "
//             "rtsp_channel, rtsp_subtype, rtsp_url, rtsp_name, "
//             "rtsp_crop_coord_x, rtsp_crop_coord_y, rtsp_crop_coord_dx, rtsp_crop_coord_dy "
//             "FROM rtsp_stream_info "
//             "ORDER BY rtsp_id"
//         );

//         nlohmann::json response_json;
//         response_json["success"] = true;
//         response_json["rtsp_sources"] = nlohmann::json::array();

//         while (auto row = result.fetchOne()) {
//             nlohmann::json source;
            
//             source["rtsp_id"]       = row[0].get<int>();
//             source["rtsp_type"]     = row[1].isNull() ? "" : row[1].get<std::string>();
//             source["rtsp_username"] = row[2].get<std::string>();
//             source["rtsp_id"]       = row[3].get<std::string>();
//             source["rtsp_port"]     = row[4].get<int>();
//             source["rtsp_channel"]  = row[5].get<std::string>();
//             source["rtsp_subtype"]  = row[6].get<std::string>();
//             source["rtsp_url"]      = row[7].get<std::string>();
//             source["rtsp_name"]     = row[8].get<std::string>();
//             if (!row[9].isNull()) source["rtsp_crop_coord_x"] = row[9].get<float>();
//             if (!row[10].isNull()) source["rtsp_crop_coord_x"] = row[10].get<float>();
//             if (!row[11].isNull()) source["rtsp_crop_coord_x"] = row[11].get<float>();
//             if (!row[12].isNull()) source["rtsp_crop_coord_x"] = row[12].get<float>();
//             response_json["rtsp_sources"].push_back(source);
//         }

//         m_response.result(http::status::ok);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = response_json.dump();

//         std::cout << "Returned " << response_json["rtsp_sources"].size() << " RTSP sources" << std::endl;
//         #endif

//         #ifdef USE_PGSQL
//         std::string sql = 
//             "SELECT rtsp_id, rtsp_type, rtsp_username, rtsp_ip, rtsp_port, "
//             "rtsp_channel, rtsp_subtype, rtsp_url, rtsp_name, "
//             "rtsp_crop_coord_x, rtsp_crop_coord_y, rtsp_crop_coord_dx, rtsp_crop_coord_dy "
//             "FROM rtsp_stream_info "
//             "ORDER BY rtsp_id";

//         pqxx::result result = execute_query(sql);

//         nlohmann::json response_json;
//         response_json["success"] = true;
//         response_json["rtsp_sources"] = nlohmann::json::array();

//         for (const auto& row: result) {
//             nlohmann::json source;
            
//             source["rtsp_id"] = row["rtsp_id"].as<int>();
//             source["rtsp_type"] = row["rtsp_type"].is_null() ? "" : row["rtsp_type"].as<std::string>();
//             source["rtsp_username"] = row["rtsp_username"].as<std::string>();
//             source["rtsp_ip"] = row["rtsp_port"].as<std::string>();
//             source["rtsp_port"] = row["rtsp_port"].as<int>();
//             source["rtsp_channel"] = row["rtsp_channel"].as<std::string>();
//             source["rtsp_subtype"] = row["rtsp_subtype"].as<std::string>();
//             source["rtsp_url"] = row["rtsp_url"].as<std::string>();
//             source["rtsp_name"] = row["rtsp_name"].as<std::string>();

//             if (!row["rtsp_crop_coord_x"].is_null()) source["rtsp_crop_coord_x"]   = row["rtsp_crop_coord_x"].as<float>();
//             if (!row["rtsp_crop_coord_y"].is_null()) source["rtsp_crop_coord_y"]   = row["rtsp_crop_coord_y"].as<float>();
//             if (!row["rtsp_crop_coord_dx"].is_null()) source["rtsp_crop_coord_dx"] = row["rtsp_crop_coord_dx"].as<float>();
//             if (!row["rtsp_crop_coord_dy"].is_null()) source["rtsp_crop_coord_dy"] = row["rtsp_crop_coord_dy"].as<float>();

//             response_json["rtsp_sources"].push_back(source);
//         }

//         m_response.result(http::status::ok);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = response_json.dump();

//         std::cout << "Returned " << result.size() << " RTSP sources" << std::endl;         
//         #endif
//     } catch (const database_exception& e) {
//         nlohmann::json error_json;
//         error_json["success"] = false;
//         error_json["error"] = "Database erro: " + std::string(e.what());
        
//         m_response.result(http::status::internal_server_error);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = error_json.dump();

//         std::cerr << "Database error: " << e.what() << std::endl;
//     } catch (const std::exception& e) {
//         nlohmann::json error_json;
//         error_json["success"] = false;
//         error_json["error"] = "Error: " + std::string(e.what());
    
//         m_response.result(http::status::internal_server_error);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = error_json.dump();
            
//         std::cerr << "Error: " << e.what() << std::endl;
//     }
// }

// void handle_add_rtsp_source(http_connection &conn) {
//     auto& m_response = conn.response();
//     auto& m_request = conn.request();
//     try {
//                     std::string body = m_request.body();    
//                     auto json = nlohmann::json::parse(body);
//                     // check url existed
//                     std::string rtsp_url = json.value("rtsp_url", "");
//                     if (rtsp_url.empty())
//                         throw std::runtime_error("rtsp_url can not be empty");
                    
//                     #ifdef USE_MYSQL
//                     auto check_result = execute_params(
//                         "SELECT rtsp_id FROM rtsp_stream_info WHERE rtsp_url = ?",
//                         rtsp_url
//                     );

//                     if (check_result.count() > 0) {
//                         auto row = check_result.fetchOne();
//                         int existing_rtsp_id = row[0].get<int>();

//                         nlohmann::json response_json;
//                         response_json["success"] = false;
//                         response_json["error"]   = "RTSP URL already exists";
//                         response_json["existing_rtsp_id"] = existing_rtsp_id;
//                         response_json["message"] = "A RTSP source with this URL already exists";

//                         m_response.result(http::status::conflict);
//                         m_response.set(http::field::content_type, "application/json");
//                         m_response.body() = response_json.dump();
                        
//                         std::cout << "RTSP URL already exists with ID: " << existing_rtsp_id << std::endl;
//                     } else {
//                         auto result = execute_params(
//                             "INSERT INTO rtsp_stream_info ("
//                             "rtsp_type, rtsp_username, rtsp_ip, rtsp_port, rtsp_channel, "
//                             "rtsp_subtype, rtsp_url, rtsp_name) "
//                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
//                             json.value("rtsp_type", ""),
//                             json.value("rtsp_username", ""),
//                             json.value("rtsp_ip", ""),
//                             json.value("rtsp_port", 0),
//                             json.value("rtsp_channel", ""),
//                             json.value("rtsp_subtype", ""),
//                             rtsp_url,
//                             json.value("rtsp_name", "无")
//                         );

//                         auto id_result = execute_query("SELECT LAST_INSERT_ID()");
//                         auto id_row = id_result.fetchOne();
//                         auto rtsp_id = id_row[0].get<int>();

//                         nlohmann::json response_json;
//                         response_json["success"] = true;
//                         response_json["rtsp_id"] = rtsp_id;
//                         response_json["message"] = "RTSP source added successfully";
                    
//                         m_response.result(http::status::ok);
//                         m_response.set(http::field::content_type, "application/json");
//                         m_response.body() = response_json.dump();

//                         std::cout << "New RTSP source added with ID: " << rtsp_id << std::endl;
//                     }
//                     #endif

//                     #ifdef USE_PGSQL
//                     std::string check_rtsp_url_existed_sql = 
//                         "SELECT rtsp_id from rtsp_stream_info WHERE rtsp_url = $1";
                        
//                     pqxx::result check_result = execute_params(check_rtsp_url_existed_sql, rtsp_url);
                        
//                         if (!check_result.empty()) {
//                             int existing_rtsp_id = check_result[0][0].as<int>();
                        
//                             nlohmann::json response_json;
//                             response_json["success"] = false;
//                             response_json["error"] = "RTSP URL already exists";
//                             response_json["existing_rtsp_id"] = existing_rtsp_id;
//                             response_json["message"] = "An RTSP source with this URL already exists";

//                             m_response.result(http::status::conflict);  // 409 conflict
//                             m_response.set(http::field::content_type, "application/json");
                            
//                             m_response.body() = response_json.dump();
//                             std::cout << "RTSP URL already exists with ID: " << existing_rtsp_id << std::endl;
//                         } else {
                    
//                             std::string sql = 
//                                 "INSERT INTO rtsp_stream_info ("
//                                 "rtsp_type, rtsp_username, rtsp_ip, rtsp_port, rtsp_channel, "
//                                 "rtsp_subtype, rtsp_url, rtsp_name) "
//                                 "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
//                                 "RETURNING rtsp_id";
                        
//                             pqxx::result result = execute_params( sql
//                                                                 , json.value("rtsp_type", "")
//                                                                 , json.value("rtsp_username", "")
//                                                                 , json.value("rtsp_ip", "")
//                                                                 , json.value("rtsp_port", 0)
//                                                                 , json.value("rtsp_channel", "")
//                                                                 , json.value("rtsp_subtype", "")
//                                                                 , rtsp_url
//                                                                 , json.value("rtsp_name", "无")
//                                                                 );
//                             int rtsp_id = result[0][0].as<int>();
                        
//                             // if inserted
//                             nlohmann::json response_json;
//                             response_json["successs"] = true;
//                             response_json["rtsp_id"] = rtsp_id;
//                             response_json["message"] = "RTSP source added successfully";
                        
//                             // response
//                             m_response.result(http::status::ok);
//                             m_response.set(http::field::content_type, "application/json");
//                             m_response.body() = response_json.dump();

//                             std::cout << "New RTSP source added with ID: " << rtsp_id << std::endl;
//                         }
//                     #endif
//                     } catch (const nlohmann::json::exception& e) {
//                         nlohmann::json error_json;
//                         error_json["success"] = false;
//                         error_json["error"] = "Invalid JSON format: " + std::string(e.what());
                        
//                         m_response.result(http::status::bad_request);
//                         m_response.set(http::field::content_type, "application/json");
//                         m_response.body() = error_json.dump();
                        
//                         std::cerr << "JSON parsing error: " << e.what() << std::endl;
//                     } catch (const database_exception& e) {
//                         nlohmann::json error_json;
//                         error_json["success"] = false;
//                         error_json["error"] = "Database error: " + std::string(e.what());

//                         m_response.result(http::status::internal_server_error);
//                         m_response.set(http::field::content_type, "application/json");
//                         m_response.body() = error_json.dump();
//                     } catch (const std::exception& e) {
//                         nlohmann::json error_json;
//                         error_json["success"] = false;
//                         error_json["error"] = "Error: " + std::string(e.what());
                        
//                         m_response.result(http::status::internal_server_error);
//                         m_response.set(http::field::content_type, "application/json");
//                         m_response.body() = error_json.dump();
                        
//                         std::cerr << "Error: " << e.what() << std::endl;
//                     } 
// }

// void handle_update_cropped_coords(http_connection &conn) {
//     auto& m_response = conn.response();
//     auto& m_request = conn.request();
    
//     try {
//         std::string body = m_request.body();
//         std::cout << "Post to /inf_qwq/update_cropped_coords: " << body << std::endl;
        
//         auto json = nlohmann::json::parse(body);

//         int rtsp_id = json["rtsp_id"];
//         float x = json["x"];
//         float y = json["y"];
//         float dx = json["dx"];
//         float dy = json["dy"];
        
//         #ifdef USE_MYSQL
//         auto check_result = execute_params(
//             "SELECT rtsp_id FROM rtsp_stream_info WHERE rtsp_id = ?",
//             rtsp_id
//         );

//         if (check_result.count() == 0) {
//             nlohmann::json error_json;
//             error_json["success"] = false;
//             error_json["error"] = "RTSP ID not found";
//             error_json["message"] = "No RTSP source exist with the provided ID: ";

//             m_response.result(http::status::not_found);
//             m_response.set(http::field::content_type, "application/json");
//             m_response.body() = error_json.dump();
            
//             std::cout << "RTSP ID not found: " << rtsp_id << std::endl;
//         } else {
//             execute_params(
//                 "UPDATE rtsp_stream_info "
//                 "SET rtsp_crop_coord_x = ?, rtsp_crop_coord_y = ?, "
//                 "rtsp_crop_coord_dx = ?, rtsp_crop_coord_dy = ? "
//                 "WHERE rtsp_id = ?",
//                 x, y, dx, dy, rtsp_id
//             );

//             inf_qwq::utils::rtsp::rtsp_capturer::instance().update_crop_coordinates(rtsp_id, x, y, dx, dy);
            
//             nlohmann::json response_json;
//             response_json["success"] = true;
//             response_json["rtsp_id"] = rtsp_id;
//             response_json["message"] = "Cropped coordinates updated successfully";
            
//             m_response.result(http::status::ok);
//             m_response.set(http::field::content_type, "application/json");
//             m_response.body() = response_json.dump();

//             std::cout << "Updated cropped coordinates for RTSP ID: " << rtsp_id
//                       << " [x= " << x
//                       << ", y= " << y
//                       << ", dx=" << dx
//                       << ", dy=" << dy
//                       << "]"     << std::endl;
//         }
//         #endif

//         #ifdef USE_PGSQL
//         std::string check_sql = "SELECT rtsp_id FROM rtsp_stream_info WHERE rtsp_id = $1";
//         pqxx::result check_result = execute_params(check_sql, rtsp_id);

//         if (check_sql.empty()) {
//             nlohmann::json error_json;
//             error_json["success"] = false;
//             error_json["error"] = "RTSP ID not found";
//             error_json["message"] = "No RTSP source exists with the provided ID";
            

//             m_response.result(http::status::not_found); // 404 NOT FOUND
//             m_response.set(http::field::content_type, "application/json");
//             m_response.body() = error_json.dump();

//             std::cout << "RTSP ID not found: " << rtsp_id << std::endl;
//         } else {
//             std::string update_sql =
//                 "UPDATE rtsp_stream_info "
//                 "SET rtsp_crop_coord_x = $1, rtsp_crop_coord_y = $2, "
//                 "rtsp_crop_coord_dx = $3, rtsp_crop_coord_dy = $4 "
//                 "WHERE rtsp_id = $5";

//             execute_params(update_sql, x, y, dx, dy, rtsp_id);
            
//             inf_qwq::utils::rtsp::rtsp_capturer::instance().update_crop_coordinates(rtsp_id, x, y, dx,  dy);
            
//             nlohmann::json response_json;
//             response_json["success"] = true;
//             response_json["rtsp_id"] = rtsp_id;
//             response_json["message"] = "Cropped coordinates updated successfully";
            
//             m_response.result(http::status::ok);
//             m_response.set(http::field::content_type, "application/json");
//             m_response.body() = response_json.dump();
            
//             std::cout << "Updated cropped coordinates for RTSP ID: " << rtsp_id
//                       << " [x="  << x 
//                       << ", y="  << y 
//                       << ", dx=" << dx 
//                       << ", dy=" << dy 
//                       << "]"     << std::endl;
    
//         } 
//         #endif
//     } catch (const nlohmann::json::exception& e) {
//         nlohmann::json error_json;
//         error_json["success"] = false;
//         error_json["error"] = "Invalid Json format: " + std::string(e.what());
        
//         m_response.result(http::status::bad_request);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = error_json.dump();

//         std::cerr << "JSON parsing error: " << e.what() << std::endl;
//     } catch (const database_exception& e) {
//         nlohmann::json error_json;
//         error_json["success"] = false;
//         error_json["error"] = "Database error: " + std::string(e.what());
        
//         m_response.result(http::status::internal_server_error);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = error_json.dump();
        
//         std::cerr << "Database error: " << e.what() << std::endl;
//     } catch (const std::exception& e) {
//         nlohmann::json error_json;
//         error_json["success"] = false;
//         error_json["error"] = "Error: " + std::string(e.what());
        
//         m_response.result(http::status::internal_server_error);
//         m_response.set(http::field::content_type, "application/json");
//         m_response.body() = error_json.dump();

//         std::cerr << "Error: " << e.what() << std::endl;
//     }  
// }


// }












