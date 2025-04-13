
/* INF_QWQ_BACKEND @CHUNHUI
 * @ Edited by ppQwQqq 
 * @ Powered by Boost
 * @ ChunHui Info
 *
 *
 * */



#include "database/db_ops.hpp"
#include <cstdlib>
#include <http/http_connection.h>
#include <http/http_server.h>

#include <database/db_conn.h>
#include <iostream>

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <utils/rtsp_capturer.h>

int main(int argc, char* argv[])
{
    using namespace inf_qwq::database::pg_sql;
    using namespace inf_qwq::utils::rtsp; 

    // rtsp_capturer k_rtsp_capturer;
    //  
    // k_rtsp_capturer.set_frame_callback([](cv::Mat mat) {
    //     cv::imwrite("rtsp", mat);  
    // });
    // k_rtsp_capturer.set_error_callback([](std::string str) { });
    //
    // k_rtsp_capturer.switch_rtsp_stream("rtsp://localhost:8554/cam");
    
   


    conn_config config;
    config.host = "localhost";
    config.port = 5432;
    config.db_name = "inf_qwq";
    config.user = "ppqwqqq";
    config.password = "20041025";

    //pg_sql_conn conn(config);

    auto& db = pg_sql_conn::get_instance(config);


    if (!db.is_initialized()) {
        std::cerr << "Failed to initialize database connection" << std::endl;
        return EXIT_FAILURE;
    }

    auto& capturer = inf_qwq::utils::rtsp::rtsp_capturer::instance("./captures");

    capturer.set_capture_interval(5);
    capturer.set_max_queue_size(200);
    capturer.set_save_to_disk(true);

    capturer.initialize();

 // 等待一段时间，让捕获器收集一些图像
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 获取最新的批次
    inf_qwq::utils::rtsp::image_batch latest_batch;
    if (capturer.get_latest_batch(latest_batch)) {
        std::cout << "Latest batch contains " << latest_batch.images.size() << " images" << std::endl;
        
        // 处理批次中的图像
        for (const auto& image : latest_batch.images) {
            std::cout << "RTSP ID: " << image.rtsp_id 
                      << ", Name: " << image.rtsp_name
                      << ", Original size: " << image.original_image.size()
                      << ", Cropped size: " << image.cropped_image.size()
                      << std::endl;
        }
    }
    
    // 弹出队首批次
    inf_qwq::utils::rtsp::image_batch front_batch;
    if (capturer.pop_front_batch(front_batch)) {
        std::cout << "Popped front batch with " << front_batch.images.size() << " images" << std::endl;
    }
    
    // 获取特定RTSP ID的最新图像
    inf_qwq::utils::rtsp::captured_image specific_image;
    if (capturer.get_latest_image(1, specific_image)) {
        std::cout << "Latest image for RTSP ID 1: " 
                  << "Original size: " << specific_image.original_image.size() 
                  << ", Cropped size: " << specific_image.cropped_image.size() 
                  << std::endl;
    }
    
    // 主循环，保持程序运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }



    using namespace inf_qwq::http;
    try
    {
        if (argc != 3)
        {
            std::cerr << "Usage: http-server-sync <address> <port>\n";
           std::cerr << "Example:\n";
            std::cerr << "    http-server-sync 0.0.0.0 8080\n";
            return EXIT_FAILURE;
        }

        auto const address = net::ip::make_address(argv[1]);
        unsigned short port = static_cast<unsigned short>(std::atoi(argv[2]));

        net::io_context ioc{1};

        http_server server{ioc, {address, port}};
        server.run();

        ioc.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

