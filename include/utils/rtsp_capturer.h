
#ifndef RTSP_CAPTURER_H
#define RTSP_CAPTURER_H


#include <opencv2/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>


namespace inf_qwq {
    namespace utils {
        namespace rtsp {
            class rtsp_capturer {
            public:
                explicit rtsp_capturer(){};
                ~rtsp_capturer() { stop(); }

                void stop() {
                    m_stop = true;
                    if (m_thread && m_thread->joinable()) m_thread->join(); 
                }
                void run() {
                    if (!m_capturer.isOpened()) {
                        if (m_error_callback) m_error_callback("RTSP stream not available");
                        return;
                    }
                
                    while (!m_stop) {
                        cv::Mat frame;
                        
                        if (!m_capturer.read(frame)) {
                            if (m_error_callback) m_error_callback("ERROR");
                            break;
                        }

                        if (frame.empty()) continue;
                        cv::imshow("rtsp", frame);
                        if (m_frame_callback) m_frame_callback(frame);
                    }
                
                    m_capturer.release();

                }

                bool switch_rtsp_stream(const std::string& rtsp_url) {
                    if (is_running()) stop();
                    if (open_rtsp_stream(rtsp_url)) {
                        m_rtsp_url = rtsp_url;
                        m_stop = false;
                        m_thread = std::make_unique<std::thread>(&rtsp_capturer::run, this);
                        return true;
                    }
                    if (m_error_callback) {
                        m_error_callback("Error");
                    }
                    return false;
                }
                bool is_running() const { return m_thread != nullptr && m_thread->joinable(); }

                using frame_callback = std::function<void(cv::Mat)>;
                using error_callback = std::function<void(const std::string&)>;
    
                void set_frame_callback(frame_callback callback) { m_frame_callback = callback; }
                void set_error_callback(error_callback callback) { m_error_callback = callback; }
    
            private:
                std::string m_rtsp_url;
                cv::VideoCapture m_capturer;
                std::atomic_bool m_stop{false};
                std::unique_ptr<std::thread> m_thread;
    
                frame_callback m_frame_callback;
                error_callback m_error_callback;

                bool open_rtsp_stream(const std::string& url) {
                    if (m_capturer.isOpened()) m_capturer.release();
                    return m_capturer.open(url);
                }
            };
        }
    }
}


#endif // RTSP_CAPTURER_H
