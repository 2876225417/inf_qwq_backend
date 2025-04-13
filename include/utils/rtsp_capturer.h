
#ifndef RTSP_CAPTURER_H
#define RTSP_CAPTURER_H


#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <database/db_ops.hpp>

namespace inf_qwq {
    namespace utils {
        namespace rtsp { 
            using namespace database::pg_sql;
        
            struct captured_image {
               int rtsp_id;
                std::string rtsp_name;
                cv::Mat original_image;
                cv::Mat cropped_image;
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

            struct image_batch {
                std::vector<captured_image> images;
                std::chrono::system_clock::time_point timestamp;
                
                image_batch() = default;

                image_batch( const std::vector<captured_image>& imgs
                           , std::chrono::system_clock::time_point time
                           ):images(imgs)
                           , timestamp(time) { }
            }; 

            class rtsp_capturer {
            public:
                rtsp_capturer(const rtsp_capturer&) = delete;
                rtsp_capturer& operator=(const rtsp_capturer&) = delete;

                static rtsp_capturer& instance(const std::string& save_dir = "./captures") {
                    static std::mutex mutex;
                    std::lock_guard<std::mutex> lock(mutex);

                    static rtsp_capturer instance(save_dir);
                    return instance;
                }

                void initialize() { 
                    fetch_rtsp_streams_from_db(); 
                    m_batch_thread = std::make_unique<std::thread>([this]() {
                        this->batch_proccessing_thread();
                    }); 
                }

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
                    std::lock_guard<std::mutex> lock(m_mutex);
                    
                    if (m_streams.find(info.rtsp_id) != m_streams.end() &&
                        m_streams[info.rtsp_id].is_running) return;
                    
                    stream_runtime& runtime = m_streams[info.rtsp_id];
                    runtime.rtsp_id = info.rtsp_id;
                    runtime.stop = false;

                    if (!runtime.capturer.open(info.rtsp_url)) {
                        std::cerr << "Failed to open RTSP stream: " <<info.rtsp_url << std::endl;
                        return;
                    }
                    
                    runtime.thread = std::make_unique<std::thread>([this, id = info.rtsp_id]() {
                        this->capture_thread(id);
                    });
                    
                    runtime.is_running = true;
                }

                void capture_thread(int rtsp_id) {
                    stream_runtime* runtime = nullptr;
                    rtsp_stream_info* info = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (m_streams.find(rtsp_id) == m_streams.end() || 
                            m_stream_infos.find(rtsp_id) == m_stream_infos.end()) return;
                        runtime = &m_streams[rtsp_id];
                        info = &m_stream_infos[rtsp_id];
                    }

                    auto last_capture_time = std::chrono::steady_clock::now();
                    
                    while (!runtime->stop) {
                        cv::Mat frame;
                        
                        if (!runtime->capturer.read(frame)) {
                            std::cerr << "Failed to read frame from RTSP stream ID " << rtsp_id << std::endl;

                            std::this_thread::sleep_for(std::chrono::seconds(1));            
                            {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                runtime->capturer.release();
                                
                                if (!runtime->capturer.open(info->rtsp_url)) {
                                    std::cerr << "Failed to reconnect to RTSP stream ID " << rtsp_id << std::endl;
                                    break;
                                }
                            }
                            continue;
                        }
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
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        runtime->capturer.release();
                        runtime->is_running = false;
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
