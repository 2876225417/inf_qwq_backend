
#ifndef RTSP_CAPTURER_H
#define RTSP_CAPTURER_H


#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <future>
#include <database/db_ops.hpp>
#ifdef USE_PGSQL
#include <pqxx/pqxx>
#endif

namespace inf_qwq {
    namespace utils {
        namespace rtsp { 
            using namespace inf_qwq::database;
            // 捕获帧的信息
            struct captured_image {
                int         rtsp_id;
                std::string rtsp_name;
                cv::Mat     original_image;
                cv::Mat     cropped_image;
                std::chrono::system_clock::time_point timestamp;

                captured_image() = default;
                captured_image( int id
                              , const std::string& name
                              , const cv::Mat& original
                              , const cv::Mat& cropped
                              , std::chrono::system_clock::time_point& time
                              ):rtsp_id(id)
                              , rtsp_name(name)
                              , original_image(original.clone())
                              , cropped_image(cropped.clone())
                              , timestamp(time) {}
            };
            // 同一时间所获取的所有捕获帧批次
            struct image_batch {
                std::vector<captured_image> images;
                std::chrono::system_clock::time_point timestamp;
                
                image_batch() = default;
                image_batch( const std::vector<captured_image>& imgs
                           , std::chrono::system_clock::time_point time
                           ):images(imgs)
                           , timestamp(time) {}
            }; 

            // RTSP 视频帧捕获器
            class rtsp_capturer {
            public:
                /*** 单例实现
                 *   启动时 (intialize())   
                 *   获取数据库中的所有 RTSP 数据
                 *   并尝试启动这些 RTSP 流
                 */
                rtsp_capturer(const rtsp_capturer&) = delete;
                rtsp_capturer& operator=(const rtsp_capturer&) = delete;

                static rtsp_capturer& instance(const std::string& save_dir = "./captures") {
                    static std::mutex mutex;
                    std::lock_guard<std::mutex> lock(mutex);
                    /* 配置文件存储路径 save_dir */
                    static rtsp_capturer instance(save_dir);
                    return instance;
                }
                void initialize() { 
                    fetch_rtsp_streams_from_db(); 
                    m_batch_thread = std::make_unique<std::thread>([this]() {
                        this->batch_proccessing_thread();
                    }); 
                }
                
                /* 添加 RTSP 流(运行时)*/
                bool add_rtsp_stream( int rtsp_id
                                    , const std::string& rtsp_url
                                    , const std::string& rtsp_name
                                    , float crop_x = 0.f, float crop_y = 0.f
                                    , float crop_dx = 1.f, float crop_dy = 1.f
                                    , const std::string& rtsp_type = ""
                                    , const std::string& rtsp_username = ""
                                    , const std::string& rtsp_ip = ""
                                    , int rtsp_port = 0
                                    , const std::string& rtsp_channel = ""
                                    , const std::string& rtsp_subtype = ""
                                    ) {
                    try {
                        std::lock_guard<std::mutex> lock(m_mutex);

                        if (m_stream_infos.find(rtsp_id) != m_stream_infos.end()) {
                            std::cerr << "RTSP stream ID " << rtsp_id << " already exists\n";
                            return false;
                        }

                        rtsp_stream_info info;
                        info.rtsp_id = rtsp_id;
                        info.rtsp_url = rtsp_url;
                        info.rtsp_name = rtsp_name;
                        info.rtsp_type = rtsp_type;
                        info.rtsp_username = rtsp_username;
                        info.rtsp_ip = rtsp_ip;
                        info.rtsp_port = rtsp_port;
                        info.rtsp_channel = rtsp_channel;
                        info.rtsp_subtype = rtsp_subtype;
                        info.crop_x = crop_x;
                        info.crop_y = crop_y;
                        info.crop_dx = crop_dx;
                        info.crop_dy = crop_dy;

                        m_stream_infos[rtsp_id] = info;
                        
                        std::cout << "Added RTSP stream ID=" << rtsp_id 
                                  << "\nURL=" << rtsp_url 
                                  << "\nName=" << rtsp_name 
                                  << "\nCrop=(" << crop_x << "," << crop_y 
                                  << "," << crop_dx << "," << crop_dy << ")"
                                  << '\n';
                        

                        start_stream(info);
                        return true;
                    } catch (const std::exception& e) {
                        std::cerr << "Error in add_rtsp_stream: " << e.what() << std::endl;
                        return false;
                    }
                }

                /* 移除RTSP流(运行时)*/
                bool remove_rtsp_stream(int rtsp_id) {
                    std::lock_guard<std::mutex> lock(m_mutex);

                    if (m_stream_infos.find(rtsp_id) == m_stream_infos.end()) {
                        std::cerr << "RTSP stream ID " << rtsp_id << "not found" << '\n';
                        return false;
                    }

                    if (m_streams.find(rtsp_id) != m_streams.end() &&
                        m_streams[rtsp_id].is_running) {
                        m_streams[rtsp_id].stop = true;
                        if (m_streams[rtsp_id].thread && m_streams[rtsp_id].thread->joinable()) {
                            m_streams[rtsp_id].thread->join();
                        }
                        m_streams.erase(rtsp_id);
                    }

                    m_stream_infos.erase(rtsp_id);
                    std::cout << "Remove RTSP stream ID=" << rtsp_id << '\n';
                    return true;
                }

                std::vector<captured_image> get_all_latest_images() {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    std::vector<captured_image> images;

                    std::map<int, captured_image> latest_images;

                    {
                        std::lock_guard<std::mutex> batch_lock(m_batch_queue_mutex);
                        for (auto it = m_batch_queue.rbegin(); it != m_batch_queue.rend(); ++it) {
                            for (const auto& img: it->images) {
                                if (latest_images.find(img.rtsp_id) == latest_images.end())
                                    latest_images[img.rtsp_id] = img;
                            }
                        }
                    }

                    for (auto& pair: m_stream_infos) {
                        int rtsp_id = pair.first;
                        std::lock_guard<std::mutex> img_lock(*pair.second.latest_image_mutex);
                        
                        if (pair.second.latest_image.original_image.data) {
                            if (latest_images.find(rtsp_id) == latest_images.end() || 
                                pair.second.latest_image.timestamp > latest_images[rtsp_id].timestamp)
                                latest_images[rtsp_id] = pair.second.latest_image;
                        }
                    }

                    for (const auto& [id, img]: latest_images)
                        images.push_back(img);
                    
                    return images;
                }

                /* 更新截取区域坐标(运行时) */
                bool update_crop_coordinates(int rtsp_id, float x, float y, float dx, float dy) {
                    std::lock_guard<std::mutex> lock(m_mutex);

                    if (m_stream_infos.find(rtsp_id) == m_stream_infos.end()) {
                        std::cerr << "RTSP stream ID " << rtsp_id << " not fount" << std::endl;
                        return false;
                    }

                    m_stream_infos[rtsp_id].crop_x = x;
                    m_stream_infos[rtsp_id].crop_y = y;
                    m_stream_infos[rtsp_id].crop_dx = dx;
                    m_stream_infos[rtsp_id].crop_dy = dy;
                    

                    std::cout << "Updated crop coordinates for RTSP ID " << rtsp_id
                              << ": (" << x << "," << y << "," << dx << "," << dy << ")" << std::endl;

                    return true;
                }



                std::vector<image_batch> get_all_batches() {
                    std::lock_guard<std::mutex> lock(m_batch_queue_mutex);
                    std::vector<image_batch> batches(m_batch_queue.begin(), m_batch_queue.end());
                    return batches;
                }

                bool get_latest_batch(image_batch& batch) {
                    std::lock_guard<std::mutex> lock(m_batch_queue_mutex);
                    if (m_batch_queue.empty()) return false;
                    batch = m_batch_queue.back();
                    return true;
                }
                
                bool pop_front_batch(image_batch& batch) {
                    std::lock_guard<std::mutex> lock(m_batch_queue_mutex); 
                    if (m_batch_queue.empty()) return false;

                    batch = m_batch_queue.front();
                    m_batch_queue.pop_front();
                    return true;
                }

                bool get_latest_image(int rtsp_id, captured_image& image) {
                    std::lock_guard<std::mutex> lock(m_batch_queue_mutex);
                    
                    for (auto it = m_batch_queue.rbegin(); it != m_batch_queue.rend(); ++it) {
                        for (const auto& img: it->images) {
                            if (img.rtsp_id == rtsp_id) {
                                image = img;
                                return true;
                            }
                        } 
                    }
                    return false;
                }

                void set_max_queue_size(size_t size) {
                    std::lock_guard<std::mutex> lock(m_batch_queue_mutex);
                    m_max_queue_size = size;
                    while (m_batch_queue.size() > m_max_queue_size) m_batch_queue.pop_front();
                }

                void set_capture_interval(int seconds) {
                    m_capture_interval = seconds;
                }

                void set_save_to_disk(bool save) {
                    m_save_to_disk = save;
                }

                void set_save_directory(const std::string& dir) {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_save_directory = dir;
                    std::filesystem::create_directory(m_save_directory);
                }

                ~rtsp_capturer() { 
                    m_running = false;
                    m_batch_cv.notify_all();
                    if (m_batch_thread && m_batch_thread->joinable()) 
                        m_batch_thread->join();
                    stop_all_streams(); 
                }

            private:
                explicit rtsp_capturer(const std::string& save_dir = "./captures")
                    : m_save_directory(save_dir)
                    , m_max_queue_size(100)
                    , m_save_to_disk(true)
                    , m_running(true)
                { std::filesystem::create_directory(m_save_directory); };

                struct rtsp_stream_info {
                    int rtsp_id;
                    std::string rtsp_type;
                    std::string rtsp_username;
                    std::string rtsp_ip;
                    int rtsp_port;
                    std::string rtsp_channel;
                    std::string rtsp_subtype;
                    std::string rtsp_url;
                    std::string rtsp_name;
                    float crop_x;
                    float crop_y;
                    float crop_dx;
                    float crop_dy;
                    
                    captured_image latest_image;
                    std::shared_ptr<std::mutex> latest_image_mutex;
                    bool has_new_image = false;

                    rtsp_stream_info(): latest_image_mutex(std::make_shared<std::mutex>()) {}
                };
                
                struct stream_runtime {
                    cv::VideoCapture capturer;
                    std::unique_ptr<std::thread> thread;
                    std::atomic_bool stop{false};
                    std::atomic_bool is_running{false};
                    int rtsp_id;
                };
                
                bool fetch_rtsp_streams_from_db() {
                    try {
                        std::string sql = 
                            "SELECT rtsp_id, rtsp_type, rtsp_username, rtsp_ip, rtsp_port, "
                            "rtsp_channel, rtsp_subtype, rtsp_url, rtsp_name, "
                            "rtsp_crop_coord_x, rtsp_crop_coord_y, rtsp_crop_coord_dx, rtsp_crop_coord_dy "
                            "FROM rtsp_stream_info "
                            "ORDER BY rtsp_id"; 
                        
                        #ifdef USE_MYSQL
                        auto result = execute_query(sql);
                        
                        stop_all_streams();

                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_streams.clear();
                        }

                        while (auto row = result.fetchOne()) {
                            rtsp_stream_info info;

                            info.rtsp_id = row[0].get<int>();
                            info.rtsp_type = row[1].isNull() ? "" : row[1].get<std::string>();
                            info.rtsp_username = row[2].get<std::string>();
                            info.rtsp_ip = row[3].get<std::string>();
                            info.rtsp_port = row[4].get<int>();
                            info.rtsp_channel = row[5].get<std::string>();
                            info.rtsp_subtype = row[6].get<std::string>();
                            info.rtsp_url = row[7].get<std::string>();
                            info.rtsp_name = row[8].get<std::string>();

                            if (!row[9].isNull() && 
                                !row[10].isNull() && 
                                !row[11].isNull() && 
                                !row[12].isNull()) {
                                info.crop_x = row[9].get<int>();
                                info.crop_y = row[10].get<int>();
                                info.crop_dx = row[11].get<int>();
                                info.crop_dy = row[12].get<int>();

                                {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    m_stream_infos[info.rtsp_id] = info;
                                }
                                
                                std::cout << "Loaded RTSP stream ID=" << info.rtsp_id
                                          << ", URL=" << info.rtsp_url
                                          << ", Crop=(" << info.crop_x << "," << info.crop_y
                                          << "," << info.crop_dx << "," << info.crop_dy << ")"
                                          << std::endl;
                                start_stream(info);
                            } else {
                                std::cout << "SKipped RTSP stream ID=" << info.rtsp_id
                                          << " (missing crop coordinates)" << std::endl;
                            }
                        }
                        #endif

                        #ifdef USE_PGSQL
                        pqxx::result result = execute_query(sql); 
                        
                        stop_all_streams();

                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_streams.clear(); 
                        }

                        for (const auto& row: result) {
                            rtsp_stream_info info;
                            
                            info.rtsp_id = row["rtsp_id"].as<int>();
                            info.rtsp_type = row["rtsp_type"].is_null() ? "" : row["rtsp_type"].as<std::string>(); 
                            info.rtsp_username = row["rtsp_username"].as<std::string>();
                            info.rtsp_ip = row["rtsp_ip"].as<std::string>();
                            info.rtsp_port = row["rtsp_port"].as<int>();
                            info.rtsp_channel = row["rtsp_channel"].as<std::string>();
                            info.rtsp_subtype = row["rtsp_subtype"].as<std::string>();
                            info.rtsp_url = row["rtsp_url"].as<std::string>();
                            info.rtsp_name = row["rtsp_name"].as<std::string>();

                            if (!row["rtsp_crop_coord_x"].is_null() &&
                                !row["rtsp_crop_coord_y"].is_null() &&
                                !row["rtsp_crop_coord_dx"].is_null() && 
                                !row["rtsp_crop_coord_dy"].is_null()) {
                                
                                info.crop_x = row["rtsp_crop_coord_x"].as<float>();
                                info.crop_y = row["rtsp_crop_coord_y"].as<float>();
                                info.crop_dx = row["rtsp_crop_coord_dx"].as<float>();
                                info.crop_dy = row["rtsp_crop_coord_dy"].as<float>();
                                
                                {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    m_stream_infos[info.rtsp_id] = info; 
                                }

                                std::cout << "Loaded RTSP stream ID=" << info.rtsp_id
                                          << ", URL=" << info.rtsp_url
                                          << ", Crop=(" << info.crop_x << "," << info.crop_y
                                          << "," << info.crop_dx << "," << info.crop_dy << ")"
                                          << std::endl;

                                start_stream(info);
                            } else { 
                                std::cout << "Skipped RTSP stream ID=" << info.rtsp_id
                                          << " (missing crop coordinates)" << std::endl;
                            }
                        }
                        #endif
                        return true;
                    } catch (const std::exception& e) {
                        std::cerr << "Database error: " << e.what() << std::endl;
                        return false;
                    }
                }

                void stop_all_streams() {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    
                    for(auto& pair: m_streams) {
                        if (pair.second.is_running) {
                            pair.second.stop = true;
                            if (pair.second.thread && pair.second.thread->joinable()) {
                                pair.second.thread->join();
                            }
                        }
                    }    
                    m_streams.clear();
                }


                void start_stream(const rtsp_stream_info& info) {
    int rtsp_id = info.rtsp_id;
    std::string rtsp_url = info.rtsp_url;
    
    std::cout << "Starting stream for RTSP ID=" << rtsp_id << " URL=" << rtsp_url << std::endl;
    
    // 在外部创建VideoCapture，避免在锁内执行耗时操作
    auto cap_ptr = std::make_shared<cv::VideoCapture>();
    
    try {
        bool should_start = false;
        bool has_existing_thread = false;
        std::unique_ptr<std::thread> thread_to_join;
        
        {
            std::cout << "Acquiring lock for RTSP ID=" << rtsp_id << std::endl;
            //std::lock_guard<std::mutex> lock(m_mutex);
            std::cout << "Lock acquired for RTSP ID=" << rtsp_id << std::endl;
            
            // 检查流是否已存在
            auto it = m_streams.find(rtsp_id);
            if (it != m_streams.end()) {
                std::cout << "Stream exists for RTSP ID=" << rtsp_id << ", is_running=" 
                          << (it->second.is_running ? "true" : "false") << std::endl;
                
                if (it->second.is_running) {
                    std::cout << "RTSP stream ID=" << rtsp_id << " is already running, skipping" << std::endl;
                    return;  // 流已经在运行，不需要重新启动
                } else {
                    std::cout << "RTSP stream ID=" << rtsp_id << " exists but not running, resetting it" << std::endl;
                    
                    // 如果有线程需要join，移动到外部变量，在锁外join
                    if (it->second.thread && it->second.thread->joinable()) {
                        it->second.stop = true;
                        thread_to_join = std::move(it->second.thread);
                        has_existing_thread = true;
                    } else {
                        it->second.capturer.release();
                    }
                }
            }
            
            // 如果没有线程需要join，初始化runtime
            if (!has_existing_thread) {
                stream_runtime& runtime = m_streams[rtsp_id];
                runtime.rtsp_id = rtsp_id;
                runtime.stop = false;
                runtime.is_running = false;
                should_start = true;
                
                std::cout << "Initialized runtime for RTSP ID=" << rtsp_id << std::endl;
            }
        }
        
        // 如果有线程需要join，在锁外执行
        if (has_existing_thread && thread_to_join) {
            std::cout << "Joining existing thread for RTSP ID=" << rtsp_id << std::endl;
            
            // 设置超时，避免无限等待
            auto future = std::async(std::launch::async, [&thread_to_join]() {
                if (thread_to_join && thread_to_join->joinable()) {
                    thread_to_join->join();
                }
            });
            
            // 等待join完成，最多5秒
            if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                std::cerr << "Joining thread timed out for RTSP ID=" << rtsp_id << ", detaching instead" << std::endl;
                if (thread_to_join && thread_to_join->joinable()) {
                    thread_to_join->detach();
                }
            } else {
                std::cout << "Successfully joined thread for RTSP ID=" << rtsp_id << std::endl;
            }
            
            // 现在可以安全地初始化runtime了
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_streams.find(rtsp_id) != m_streams.end()) {
                    stream_runtime& runtime = m_streams[rtsp_id];
                    runtime.rtsp_id = rtsp_id;
                    runtime.stop = false;
                    runtime.is_running = false;
                    runtime.capturer.release();  // 确保释放旧的capturer
                    should_start = true;
                    
                    std::cout << "Initialized runtime after joining thread for RTSP ID=" << rtsp_id << std::endl;
                }
            }
        }
        
        if (should_start) {
            std::cout << "Starting opener thread for RTSP ID=" << rtsp_id << std::endl;
            
            // 创建一个分离线程来打开RTSP流
            std::thread opener([this, rtsp_id, rtsp_url, cap_ptr]() {
                std::cout << "Opener thread started for RTSP ID=" << rtsp_id << std::endl;
                
                try {
                    // 设置打开超时
                    #if CV_VERSION_MAJOR >= 3
                    cap_ptr->set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000);  // 5秒超时
                    #endif
                    
                    std::cout << "Attempting to open RTSP URL: " << rtsp_url << " for ID=" << rtsp_id << std::endl;
                    
                    // 使用超时机制打开RTSP流
                    std::atomic<bool> open_completed{false};
                    std::atomic<bool> open_success{false};
                    
                    // 创建一个线程来执行打开操作
                    std::thread open_thread([&cap_ptr, &rtsp_url, &open_completed, &open_success]() {
                        try {
                            open_success = cap_ptr->open(rtsp_url);
                            open_completed = true;
                        } catch (const cv::Exception& e) {
                            std::cerr << "OpenCV exception when opening RTSP stream: " << e.what() << std::endl;
                            open_completed = true;
                        } catch (const std::exception& e) {
                            std::cerr << "Exception when opening RTSP stream: " << e.what() << std::endl;
                            open_completed = true;
                        }
                    });
                    
                    // 等待打开完成或超时
                    {
                        auto start_time = std::chrono::steady_clock::now();
                        const int timeout_seconds = 10;  // 10秒超时
                        
                        while (!open_completed) {
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - start_time).count();
                            
                            if (elapsed >= timeout_seconds) {
                                std::cerr << "Open operation timed out after " << elapsed 
                                          << " seconds for RTSP ID=" << rtsp_id << std::endl;
                                break;
                            }
                            
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }
                    
                    // 如果线程还在运行，分离它
                    if (open_thread.joinable()) {
                        if (open_completed) {
                            open_thread.join();
                        } else {
                            std::cerr << "Detaching open thread for RTSP ID=" << rtsp_id << std::endl;
                            open_thread.detach();
                            return;  // 超时，退出
                        }
                    }
                    
                    if (!open_success) {
                        std::cerr << "Failed to open RTSP stream: " << rtsp_url << " for ID=" << rtsp_id << std::endl;
                        return;
                    }
                    
                    std::cout << "Successfully opened RTSP stream for ID=" << rtsp_id << std::endl;
                    
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        
                        if (m_streams.find(rtsp_id) == m_streams.end()) {
                            std::cerr << "RTSP stream ID: " << rtsp_id << " was removed while opening" << std::endl;
                            return;
                        }
                        
                        stream_runtime& runtime = m_streams[rtsp_id];
                        
                        // 检查是否有旧线程需要清理
                        if (runtime.thread && runtime.thread->joinable()) {
                            std::cerr << "Warning: Thread still exists for RTSP ID=" << rtsp_id 
                                      << ", marking it to stop" << std::endl;
                            runtime.stop = true;
                            // 不在锁内join，避免死锁
                        }
                        
                        runtime.capturer = std::move(*cap_ptr);
                        
                        std::cout << "Creating capture thread for RTSP ID=" << rtsp_id << std::endl;
                        
                        try {
                            runtime.thread = std::make_unique<std::thread>([this, id = rtsp_id]() {
                                std::cout << "Capture thread started for RTSP ID=" << id << std::endl;
                                this->capture_thread(id); 
                            });
                            
                            runtime.is_running = true;
                            std::cout << "Started RTSP stream ID=" << rtsp_id << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Failed to create capture thread for RTSP stream ID " 
                                      << rtsp_id << ": " << e.what() << std::endl;
                            runtime.capturer.release();
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error in opener thread for RTSP ID " << rtsp_id << ": " << e.what() << std::endl;
                }
                
                std::cout << "Opener thread completed for RTSP ID=" << rtsp_id << std::endl;
            });
            
            std::cout << "Detaching opener thread for RTSP ID=" << rtsp_id << std::endl;
            opener.detach();
        }
        
        std::cout << "Exiting start_stream for RTSP ID=" << rtsp_id << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception in start_stream for RTSP ID=" << rtsp_id << ": " << e.what() << std::endl;
    }
}

                void capture_thread(int rtsp_id) {
                    stream_runtime* runtime = nullptr;
                    rtsp_stream_info* info = nullptr;
                    std::string rtsp_url;
                    try {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (m_streams.find(rtsp_id) == m_streams.end() || 
                            m_stream_infos.find(rtsp_id) == m_stream_infos.end()) {
                            std::cerr << "RTSP stream ID " << rtsp_id << " not found in capture_thread" << std::endl;
                            return;
                        }
                        runtime = &m_streams[rtsp_id];
                        info = &m_stream_infos[rtsp_id];
                        rtsp_url = info->rtsp_url;
                    } catch (const std::exception& e) {
                        std::cerr << "Error intializing capture thread: " << e.what() << std::endl;
                        return;
                    }

                    auto last_capture_time = std::chrono::steady_clock::now();
                    int consecutive_failures = 0;
                    const int max_failures = 10;

                    while (!runtime->stop) {
                        try {
                            cv::Mat frame;
                            bool read_success = false;
                            
                            try {
                                read_success = runtime->capturer.read(frame);
                            } catch (const cv::Exception& e) {
                                std::cerr << "OpenCV exception reading frame: " << e.what() << std::endl;
                            } catch (const std::exception& e) {
                                std::cerr << "Exception reading frame: " << e.what() << std::endl;
                            }

                            if (!read_success) {
                                std::cerr << "Failed to read frame from RTSP stream ID " << rtsp_id << std::endl;
                                consecutive_failures++;

                                if (consecutive_failures > max_failures) {
                                    std::cerr << "Too many consecutive failures, stopping RTSP stream ID " << rtsp_id << std::endl;
                                    break;
                                }

                                std::this_thread::sleep_for(std::chrono::seconds(1));            
                                try {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    runtime->capturer.release();
                                
                                    #if CV_VERSION_MAJOR >= 3
                                    runtime->capturer.set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000);
                                    #endif

                                    if (!runtime->capturer.open(info->rtsp_url)) {
                                        std::cerr << "Failed to reconnect to RTSP stream ID " << rtsp_id << std::endl;
                                        break;
                                    }
                                } catch (const std::exception& e) {
                                    std::cerr << "Error reconnecting to stream: " << e.what() << std::endl;
                                }
                                continue;
                            } consecutive_failures = 0;

                            if (frame.empty()) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                continue;
                            }

                            auto now = std::chrono::steady_clock::now();
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_capture_time).count();
                    
                            if (elapsed >= m_capture_interval) {
                                float crop_x, crop_y, crop_dx, crop_dy;  
                                std::string rtsp_name;

                                {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    crop_x = info->crop_x;
                                    crop_y = info->crop_y;
                                    crop_dx = info->crop_dx;
                                    crop_dy = info->crop_dy;
                                    rtsp_name = info->rtsp_name;
                                }
                            
                                cv::Mat cropped_frame = crop_frame(frame, crop_x, crop_y, crop_dx, crop_dy);
                        
                                auto capture_time = std::chrono::system_clock::now(); 
                                captured_image captured(rtsp_id, rtsp_name, frame, cropped_frame, capture_time);
                                {
                                    std::lock_guard<std::mutex> lock(*info->latest_image_mutex);
                                    info->latest_image = captured;
                                    info->has_new_image = true;
                                }
                            
                                m_batch_cv.notify_one();
                                if (m_save_to_disk)
                                    save_frame(rtsp_id, cropped_frame, capture_time);
                                last_capture_time = now;
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        } catch (const std::exception& e) {
                            std::cerr << "Error in capture thread for RTSP ID " << rtsp_id << ": " << e.what() << std::endl;
                            std::this_thread::sleep_for(std::chrono::seconds(1)); 
                        }
                    }
                    
                    try {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        runtime->capturer.release();
                        runtime->is_running = false;
                        std::cout << "RTSP stream ID " << rtsp_id << " stopped" << std::endl;
                    }  catch (const std::exception& e) {
                        std::cerr << "Error stopping stream: " << e.what() << std::endl;
                    }
                }

                void batch_proccessing_thread() {
                    while (m_running) {
                        std::unique_lock<std::mutex> lock(m_batch_mutex);
                    
                        m_batch_cv.wait_for(lock, std::chrono::seconds(m_capture_interval), [this]() {
                            return !m_running || has_new_images();
                        });
                        
                        if (!m_running) break;
                    
                        std::vector<captured_image> batch_images;
                        auto batch_time = std::chrono::system_clock::now();

                        {
                            std::lock_guard<std::mutex> info_lock(m_mutex);
                            for (auto& pair: m_stream_infos) {
                                std::lock_guard<std::mutex> img_lock(*pair.second.latest_image_mutex);
                                if (pair.second.has_new_image) {
                                    batch_images.push_back(pair.second.latest_image);
                                    pair.second.has_new_image = false;
                                }
                            }
                        }

                        if (!batch_images.empty()) add_batch_to_queue(batch_images, batch_time); 
                    }
                }

                bool has_new_images() {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (const auto& pair: m_stream_infos) {
                        std::lock_guard<std::mutex> img_lock(*pair.second.latest_image_mutex);  
                        if (pair.second.has_new_image) return true;
                    }
                    return false;
                }

                void add_batch_to_queue(const std::vector<captured_image>& images, std::chrono::system_clock::time_point time) {
                    std::lock_guard<std::mutex> lock(m_batch_queue_mutex);
                    image_batch batch(images, time);
                    m_batch_queue.push_back(batch);
                    while (m_batch_queue.size() > m_max_queue_size) m_batch_queue.pop_front();
                }
                
                cv::Mat crop_frame(const cv::Mat& frame, float crop_x, float crop_y, float crop_dx, float crop_dy) {
        
                    int width = frame.cols;
                    int height = frame.rows;
                    
                    int x = static_cast<int>(crop_x * width);
                    int y = static_cast<int>(crop_y * height);
                    int w = static_cast<int>(crop_dx * width);
                    int h = static_cast<int>(crop_dy * height);

                    x = std::max(0, std::min(x, width - 1));
                    y = std::max(0, std::min(y, height - 1));
                    w = std::max(1, std::min(w, width - x));
                    h = std::max(1, std::min(h, height - y));

                    cv::Rect roi(x, y, w, h);
                    return frame(roi).clone();
                }

                void save_frame(int rtsp_id, const cv::Mat& frame, std::chrono::system_clock::time_point time) {
                    auto time_t = std::chrono::system_clock::to_time_t(time);
                    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;
                    
                    std::tm tm_time;
                    #ifdef _WIN32
                        localtime_s(&now_tm, &now_time_t);
                    #else
                        localtime_r(&time_t, &tm_time);
                    #endif
                    
                    char timestamp[32];
                    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_time);

                    std::string filename = m_save_directory + "/rtsp_"      
                                         + std::to_string(rtsp_id) + "_" 
                                         + timestamp + "_"   
                                         + std::to_string(now_ms) + ".jpg";

                    try {
                        cv::imwrite(filename, frame);
                        std::cout << "Saved frame: " << filename << std::endl;
                    } catch(const cv::Exception& e) {
                        std::cerr << "Error saving frame: " << e.what() << std::endl;
                    }
                }
    
                std::map<int, rtsp_stream_info> m_stream_infos;
                std::map<int, stream_runtime> m_streams;
                std::mutex m_mutex;
                std::string m_save_directory;
                std::atomic<int> m_capture_interval{5};
                std::atomic<bool> m_save_to_disk{true};
                    
                std::deque<image_batch> m_batch_queue;
                std::mutex m_batch_queue_mutex;
                std::atomic<size_t> m_max_queue_size;
               
                std::mutex m_batch_mutex;
                std::condition_variable m_batch_cv;
                std::unique_ptr<std::thread> m_batch_thread;
                std::atomic<bool> m_running;
            };
        }
    }
}


#endif // RTSP_CAPTURER_H
