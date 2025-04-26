#include <database/db_ops.hpp>
#include "inferer/chars_ort_inferer.hpp"
#include <chrono>
#include <cstdlib>
#include <http/http_server.h>
#include <database/db_conn.h>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <utils/rtsp_capturer.h>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>

#ifdef USE_MYSQL
#include <mysqlx/xdevapi.h>
#endif


using namespace inf_qwq::utils::rtsp;

namespace inf_qwq::http{
std::shared_ptr<chars_ort_inferer> g_ort_inferer;
}

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

void run_inference(std::shared_ptr<chars_ort_inferer> inferer, rtsp_capturer& capturer) {
    try {
        // 设置结果回调函数
        inferer->set_completion_callback([](int cam_id, const std::vector<std::string>& texts) {
            // 在这里处理识别结果
            std::cout << "Camera ID: " << cam_id << ", Detected texts: ";
            if (texts.empty()) {
                std::cout << "None";
            } else {
                for (const auto& text : texts) {
                    std::cout << "\"" << text << "\" ";
                }
            }
            std::cout << std::endl;

        });

        while (g_running) {
            image_batch batch;
            if (capturer.pop_front_batch(batch)) {
                std::vector<std::pair<int, cv::Mat>> frames;
                frames.reserve(batch.images.size());
                for (const auto& image : batch.images) {
                    frames.emplace_back(image.rtsp_id, image.cropped_image.clone());
                }
                inferer->run_inf_batch(frames);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // 等待所有推理任务完成
        inferer->wait_for_completion(5000);  // 5秒超时
    } catch (const std::exception& e) {
        std::cerr << "Inference thread error: " << e.what() << std::endl;
        g_running = false;
    }
}


void run_rtsp_capturer(const std::string& capture_dir) {
    try {
        auto& capturer = rtsp_capturer::instance(capture_dir);
        capturer.set_capture_interval(1);
        capturer.set_max_queue_size(200);
        capturer.set_save_to_disk(true);

        std::cout << "Initializing RTSP capturer..." << std::endl;
        capturer.initialize();
        std::cout << "RTSP capturer initialized successfully" << std::endl;

        while (g_running) {
            image_batch latest_batch;
            if (capturer.get_latest_batch(latest_batch)) {
                std::cout << "Latest batch contains " << latest_batch.images.size() << " images" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
        
        std::cout << "RTSP capturer shutting down..." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "RTSP capturer error: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    using namespace inf_qwq::database;
    using namespace inf_qwq::utils::rtsp;
    using namespace inf_qwq::http;
    g_ort_inferer = std::make_shared<chars_ort_inferer>();

    g_ort_inferer->set_completion_callback([](int cam_id, const std::vector<std::string>& texts) {
        std::cout << "Camera ID: " << cam_id << ", Detected texts: ";
        if (texts.empty()) std::cout << "None";
        else for (const auto& text: texts) std::cout << "\"" << text << "\"";
        std::cout << std::endl;
    });

    connection_config config{"localhost", 5432, "inf_qwq", "ppqwqqq", "20041025"};

    try {
        #ifdef USE_MYSQL
        auto& db = mysql_connection::get_instance(config);
        #endif
        #ifdef USE_PGSQL
        auto& db = pg_connection::get_instance(config);
        #endif
        if (!db.is_initialized()) {
            std::cerr << "Failed to initialize database connection" << std::endl;
            return EXIT_FAILURE;
        }
        std::cout << "Database connection initialized successfully" << std::endl;

        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <address> <port>\n";
            std::cerr << "Example:\n";
            std::cerr << "    " << argv[0] << " 0.0.0.0 8080\n";
            return EXIT_FAILURE;
        }

        auto const address = inf_qwq::http::net::ip::make_address(argv[1]);
        unsigned short port = static_cast<unsigned short>(std::atoi(argv[2]));

        auto& capturer = rtsp_capturer::instance("./captures");
        capturer.set_capture_interval(1);
        capturer.set_max_queue_size(200);
        capturer.set_save_to_disk(true);
        capturer.initialize();

        inf_qwq::http::net::io_context ioc{1};
        inf_qwq::http::http_server server{ioc, {address, port}};
        server.run();
        
        std::thread http_thread([&ioc]() {
            try {
                ioc.run();
            } catch (const std::exception& e) {
                std::cerr << "HTTP server error: " << e.what() << std::endl;
                g_running = false;
            }
        });

        // std::thread inference_thread(run_inference, g_ort_inferer, std::ref(capturer));

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        


        std::cout << "Stopping all services..." << std::endl;
        ioc.stop();
        
        if (http_thread.joinable()) {
            http_thread.join();
        }
        
        // if (inference_thread.joinable()) {
        //     inference_thread.join();
        // }
        
        g_ort_inferer.reset();  // Explicitly reset the shared_ptr

        std::cout << "All services stopped. Exiting." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
