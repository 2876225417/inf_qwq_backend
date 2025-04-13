#include "database/db_ops.hpp"
#include <cstdlib>
#include <http/http_connection.h>
#include <http/http_server.h>
#include <database/db_conn.h>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <utils/rtsp_capturer.h>
#include <thread>
#include <atomic>
#include <csignal>

// 全局变量用于控制程序运行
std::atomic<bool> g_running{true};

// 信号处理函数
void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

// RTSP Capturer 运行函数
void run_rtsp_capturer(const std::string& capture_dir) {
    try {
        auto& capturer = inf_qwq::utils::rtsp::rtsp_capturer::instance(capture_dir);

        capturer.set_capture_interval(5);
        capturer.set_max_queue_size(200);
        capturer.set_save_to_disk(true);

        std::cout << "Initializing RTSP capturer..." << std::endl;
        capturer.initialize();
        std::cout << "RTSP capturer initialized successfully" << std::endl;

        // 主循环，定期处理和报告捕获的图像
        while (g_running) {
            // 获取最新的批次
            inf_qwq::utils::rtsp::image_batch latest_batch;
            if (capturer.get_latest_batch(latest_batch)) {
                std::cout << "Latest batch contains " << latest_batch.images.size() << " images" << std::endl;
                
                // 可以在这里添加更多的批次处理逻辑
            }
            
            // 等待一段时间再进行下一次处理
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
        
        std::cout << "RTSP capturer shutting down..." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "RTSP capturer error: " << e.what() << std::endl;
    }
}

#include <inferer/chars_inferer.hpp>
#include <inferer/ort_inf.hpp>


int main(int argc, char* argv[]) {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    using namespace inf_qwq::database::pg_sql;
    using namespace inf_qwq::utils::rtsp;
    
    chars_ort_inferer* ort_inferer = new chars_ort_inferer();

    // 数据库配置
    conn_config config;
    config.host = "localhost";
    config.port = 5432;
    config.db_name = "inf_qwq";
    config.user = "ppqwqqq";
    config.password = "20041025";

    try {
        // 初始化数据库连接
        auto& db = pg_sql_conn::get_instance(config);

        if (!db.is_initialized()) {
            std::cerr << "Failed to initialize database connection" << std::endl;
            return EXIT_FAILURE;
        }
        
        std::cout << "Database connection initialized successfully" << std::endl;

        // 检查命令行参数
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <address> <port>\n";
            std::cerr << "Example:\n";
            std::cerr << "    " << argv[0] << " 0.0.0.0 8080\n";
            return EXIT_FAILURE;
        }

        auto const address = inf_qwq::http::net::ip::make_address(argv[1]);
        unsigned short port = static_cast<unsigned short>(std::atoi(argv[2]));

        // 启动RTSP捕获器线程
        std::thread rtsp_thread(run_rtsp_capturer, "./captures");
        
        // 设置线程为分离状态，这样主线程结束时不会等待它
        rtsp_thread.detach();
        
        std::cout << "RTSP capturer thread started" << std::endl;

        // 启动HTTP服务器
        inf_qwq::http::net::io_context ioc{1};
        inf_qwq::http::http_server server{ioc, {address, port}};
        
        std::cout << "HTTP server starting on " << address << ":" << port << std::endl;
        
        // 启动服务器
        server.run();
        
        // 运行io_context
        std::thread http_thread([&ioc]() {
            try {
                ioc.run();
            }
            catch (const std::exception& e) {
                std::cerr << "HTTP server error: " << e.what() << std::endl;
                g_running = false;
            }
        });

        std::cout << "HTTP server thread started" << std::endl;
        
        // 主线程等待退出信号
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // 停止HTTP服务器
        std::cout << "Stopping HTTP server..." << std::endl;
        ioc.stop();
        
        // 等待HTTP线程结束
        if (http_thread.joinable()) {
            http_thread.join();
        }
        
        std::cout << "All services stopped. Exiting." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

