#ifndef ORT_INF_HPP
#define ORT_INF_HPP

#include "onnxruntime_c_api.h"
#include "opencv2/core/types.hpp"
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

#include <sstream>
#include <stdexcept>
#include <string>

#ifdef ENABLE_EIGEN
#include <eigen3/Eigen/Dense>
#endif

#include <chrono>
#ifdef ENABLE_OMP
#include <omp.h>
#endif

#include <thread>
#include <any>

struct det_post_args{ cv::Mat resized_img; };
struct rec_post_args{};

struct output_values{
    float* output_tensor;
    std::vector<int64_t> output_shape;
};

template <typename InputType, typename OutputType>
class common_inferer {
protected:
    Ort::Env m_env;
    Ort::SessionOptions m_session_options;
    Ort::Session m_session;
    
    // 缓存输入输出名称，避免每次推理都重新获取
    std::string m_input_name;
    std::string m_output_name;
    
    // 预分配内存信息
    Ort::MemoryInfo m_memory_info{nullptr};
    
    // 输入输出形状缓存
    std::vector<int64_t> m_last_input_shape;
    
    // 预分配的输入tensor值
    std::vector<float> m_input_tensor_values;

    #ifdef ENABLE_EIGEN
    virtual std::tuple<std::vector<float>, cv::Mat>
    preprocess_eigen(InputType&) = 0;
    #else
    virtual std::tuple<std::vector<float>, cv::Mat>
    preprocess(InputType&) = 0;
    #endif

    virtual OutputType infer(InputType&) = 0;

    virtual OutputType 
    postprocess( float*& output_tensor
               , std::vector<int64_t>& output_shape
               , const std::any& additional_args) = 0;
public:
    common_inferer(const std::string& model_path)
        : m_env(ORT_LOGGING_LEVEL_WARNING)
        , m_session_options{}
        , m_session{nullptr}
        , m_memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)}
        {
        const int num_cpu_cores = std::thread::hardware_concurrency();
        m_session_options.SetIntraOpNumThreads(num_cpu_cores / 16);
        m_session_options.SetInterOpNumThreads(1);
        m_session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
        m_session_options.EnableCpuMemArena();
        m_session_options.EnableMemPattern();
        m_session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_session_options.AddConfigEntry("session.use_device_allocator_for_initializers", "1"); 
        m_session = Ort::Session(m_env, model_path.c_str(), m_session_options);


        Ort::AllocatorWithDefaultOptions allocator;
        Ort::AllocatedStringPtr input_name_ptr = m_session.GetInputNameAllocated(0, allocator);
        Ort::AllocatedStringPtr output_name_ptr = m_session.GetOutputNameAllocated(0, allocator);
        m_input_name = input_name_ptr.get();
        m_output_name = output_name_ptr.get();


    }
    
    ~common_inferer() = default;
};

class det_inferer: public common_inferer<cv::Mat, std::vector<cv::Mat>> {
private:
    // 预分配的输出缓冲区
    std::vector<std::vector<cv::Point>> m_boxes_cache;

public:
    
    inline std::vector<cv::Mat>
    postprocess( float*& output_tensor
               , std::vector<int64_t>& output_shape
               , const std::any& additional_args) override {
        const auto& args = std::any_cast<det_post_args>(additional_args);
        cv::Mat resized_img = args.resized_img;
        std::vector<cv::Mat> croppeds{};
        int output_h = output_shape[2];
        int output_w = output_shape[3];

        std::vector<float> output_data(output_tensor, output_tensor + output_w * output_h);
        auto boxes = process_detection_output(output_data, output_h, output_w);

        cv::Mat vis_img;
        cv::cvtColor(resized_img, vis_img, cv::COLOR_RGB2BGR);
        
        croppeds.reserve(boxes.size()); // 预分配内存
        for (size_t i = 0; i < boxes.size(); i++) {
            float horizontal_ratio = 0.2;
            float vertical_ratio = 0.5;

            cv::Rect expanded_rect = expand_box(boxes[i], horizontal_ratio, vertical_ratio, vis_img.size());
            cv::Mat cropped = vis_img(expanded_rect).clone();
            croppeds.push_back(cropped);
        }
        return croppeds;
    }

    inline std::vector<cv::Mat>
    infer(cv::Mat& frame) override {
        std::vector<cv::Mat> det_croppeds{}; 
        //try {
            auto start_preprocess_det = std::chrono::high_resolution_clock::now(); 
            
            cv::Mat resized_img;
            std::vector<float> input_tensor_values;
            
            #ifdef ENABLE_EIGEN
            std::tie(input_tensor_values, resized_img) = preprocess_eigen(frame);
            #else
            std::tie(input_tensor_values, resized_img) = preprocess(frame);
            #endif

            auto end_process_det = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_process_det - start_preprocess_det);
   

            // 准备输入tensor
            std::vector<int64_t> input_shape = {1, 3, resized_img.rows, resized_img.cols};
            
            // 检查形状是否变化，如果变化则需要重新创建tensor
            bool shape_changed = (m_last_input_shape != input_shape);
            if (shape_changed) {
                m_last_input_shape = input_shape;
            }
            
            // 创建输入tensor
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                m_memory_info, input_tensor_values.data(), input_tensor_values.size(),
                input_shape.data(), input_shape.size());

            // 运行推理
            const char* input_names[] = {m_input_name.c_str()};
            const char* output_names[] = {m_output_name.c_str()};
            std::vector<Ort::Value> outputs = m_session.Run(
                    Ort::RunOptions{nullptr},
                    input_names, &input_tensor, 1,
                    output_names, 1);


            // 处理输出
            auto output_tensor = outputs[0].GetTensorMutableData<float>();
            auto output_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            det_post_args args{resized_img}; 
            return postprocess(output_tensor, output_shape, args);
       /* } catch (const Ort::Exception& e) {
            
        } catch (const std::exception& e) {
           
        }
       */
        return det_croppeds;
    }

    #ifdef ENABLE_EIGEN
    std::tuple<std::vector<float>, cv::Mat>
    preprocess_eigen(cv::Mat& frame)  override {
        if (frame.empty()) { qDebug() << "Invalid input frame"; }

        cv::Mat rgb_img;
        cv::cvtColor(frame, rgb_img, cv::COLOR_BGR2RGB);

        int max_size = 960;
        float scale = max_size / static_cast<float>(std::max(rgb_img.cols, rgb_img.rows));

        int new_w = static_cast<int>(rgb_img.cols * scale) / 32 * 32;
        int new_h = static_cast<int>(rgb_img.rows * scale) / 32 * 32;
        cv::Mat resized_img;
        cv::resize(rgb_img, resized_img, cv::Size(new_w, new_h));

        cv::Mat float_img;
        resized_img.convertTo(float_img, CV_32FC3, 1.f / 255.f);

        // 预分配内存
        if (m_input_tensor_values.size() < 1 * 3 * new_h * new_w) {
            m_input_tensor_values.resize(1 * 3 * new_h * new_w);
        }

        Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            eigen_img((float*)float_img.data, new_h, new_w * 3);

        #pragma omp parallel for collapse(2)
        for (int c = 0; c < 3; c++) {
            for (int h =0; h < new_h; h++) {
                #pragma omp simd
                for (int w = 0; w < new_w; w++) {
                    m_input_tensor_values[c * new_h * new_w + h * new_w + w] = eigen_img(h, w * 3 + c);
                }
            }
        }
        return {m_input_tensor_values, resized_img};
    }
    #else
    std::tuple<std::vector<float>, cv::Mat>
    preprocess(cv::Mat& frame) override {
        if (frame.empty()) {  }

        cv::Mat rgb_img;
        cv::cvtColor(frame, rgb_img, cv::COLOR_BGR2RGB);

        int max_size = 960;
        float scale = max_size / static_cast<float>(std::max(rgb_img.cols, rgb_img.rows));
        
        int new_w = static_cast<int>(rgb_img.cols * scale) / 32 * 32;
        int new_h = static_cast<int>(rgb_img.rows * scale) / 32 * 32;
        cv::Mat resized_img;
        cv::resize(rgb_img, resized_img, cv::Size(new_w, new_h));

        cv::Mat float_img;
        resized_img.convertTo(float_img, CV_32FC3, 1.0 / 255.0);
        
        // 预分配内存
        if (m_input_tensor_values.size() < 1 * 3 * new_h * new_w) {
            m_input_tensor_values.resize(1 * 3 * new_h * new_w);
        }

        for (int c = 0; c < 3; c++) {
            for (int h = 0; h < new_h; h++) {
                for (int w = 0; w < new_w; w++) {
                    m_input_tensor_values[c * new_h * new_w + h * new_w + w] = 
                        float_img.at<cv::Vec3f>(h, w)[c];
                }
            }
        }
        return  { m_input_tensor_values, resized_img };
    }
    #endif

    inline std::vector<std::vector<cv::Point>>
    process_detection_output( const std::vector<float>& output
                            , int height, int width
                            , float threshold = 0.3f
                            ) {
        cv::Mat score_map( height, width
                         , CV_32F
                         , const_cast<float*>(output.data())
                         ) ;

        cv::Mat binary_map;
        cv::threshold(score_map, binary_map, threshold, 255, cv::THRESH_BINARY);
        binary_map.convertTo(binary_map, CV_8UC1);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary_map, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        // 重用boxes缓存
        m_boxes_cache.clear();
        m_boxes_cache.reserve(contours.size());
        
        for (const auto& cnt: contours) {
            cv::RotatedRect rect = cv::minAreaRect(cnt);
            cv::Point2f vertices[4];
            rect.points(vertices);

            std::vector<cv::Point> box;
            box.reserve(4);
            for (int i = 0; i < 4; i++) {
                box.push_back(cv::Point( static_cast<int>(vertices[i].x)
                                       , static_cast<int>(vertices[i].y))
                                       ) ;
            }
            m_boxes_cache.push_back(box);
        }
        return m_boxes_cache;
    }

    inline cv::Rect 
    expand_box( const std::vector<cv::Point>& box
              , float horizontal_ratio
              , float vertical_ratio
              , const cv::Size& img_shape
              ) {
        cv::Rect rect = cv::boundingRect(box);

        int dx = static_cast<int>(rect.width * horizontal_ratio);
        int dy = static_cast<int>(rect.height * vertical_ratio);

        int new_x = std::max(0, rect.x - dx);
        int new_y = std::max(0, rect.y - dy);
        int new_w = std::min(img_shape.width - new_x, rect.width + 2 * dx);
        int new_h = std::min(img_shape.height - new_y, rect.height + 2 * dy);

        return cv::Rect{new_x, new_y, new_w, new_h};
    }

public:
    det_inferer(const std::string& model_path = "det_server.onnx")
        : common_inferer<cv::Mat, std::vector<cv::Mat>>(model_path) 
        { }

    std::vector<cv::Mat> run_inf(cv::Mat& frame) { return infer(frame); }
    void set_det_threshold(float threshold) { }
};

#include <fstream>

class rec_inferer: public common_inferer<cv::Mat, std::string> {
private:
    std::vector<std::string> m_char_dict;
    const int TARGET_HEIGHT = 48;
    
    // 缓存预处理和后处理结果
    std::vector<int> m_sequence_preds;
    std::vector<std::string> m_result_cache;

    void load_chars(const std::string dict_path) {
        m_char_dict.clear();
        std::ifstream file(dict_path.c_str());
        std::string line;
        if (!file.is_open()) throw std::runtime_error("Can not open chars dict: " + dict_path);

        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            if (!line.empty()) m_char_dict.push_back(line);
        }
    }

    // preprocess
    #ifdef ENABLE_EIGEN
    inline std::tuple<std::vector<float>, cv::Mat>
    preprocess_eigen(cv::Mat& frame) override {
        cv::Mat img = frame.clone();
        if (img.empty()) throw std::runtime_error("Invalid input frame");

        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        
        int width = static_cast<int>(img.cols * (static_cast<float>(TARGET_HEIGHT) / img.rows));

        cv::Mat resized_img;
        cv::resize(img, resized_img, cv::Size(width, TARGET_HEIGHT));

        resized_img.convertTo(resized_img, CV_32F, 1.f / 255.f);

        // 预分配内存
        if (m_input_tensor_values.size() < TARGET_HEIGHT * width * 3) {
            m_input_tensor_values.resize(TARGET_HEIGHT * width * 3);
        }

        Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            eigen_img((float*)resized_img.data, TARGET_HEIGHT, width * 3);

        #pragma omp parallel for collapse(2) 
        for (int c = 0; c < 3; c++) {
            for (int h = 0; h < TARGET_HEIGHT; h++) {
                #pragma omp simd
                for (int w = 0; w < width; w++) {
                    m_input_tensor_values[c * TARGET_HEIGHT * width + h * width + w] = eigen_img(h, w * 3 + c);
                }
            }
        }
        return {m_input_tensor_values, resized_img};
    }
    #else
    inline std::tuple<std::vector<float>, cv::Mat>
    preprocess(cv::Mat& frame) override {
        cv::Mat img = frame.clone();
        if (img.empty()) throw std::runtime_error("Invalid input frame!");

        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);

        int width = static_cast<int>(img.cols * (static_cast<float>(TARGET_HEIGHT) / img.rows));
        
        cv::Mat resized_img;
        cv::resize(img, resized_img, cv::Size(width, TARGET_HEIGHT));

        resized_img.convertTo(resized_img, CV_32F, 1.f / 255.f);

        // 预分配内存
        if (m_input_tensor_values.size() < TARGET_HEIGHT * width * 3) {
            m_input_tensor_values.resize(TARGET_HEIGHT * width * 3);
        }
        
        size_t idx = 0;
        for (int c = 0; c < 3; c++) {
            for (int h = 0; h < resized_img.rows; h++) {
                for (int w = 0; w < resized_img.cols; w++) {
                    m_input_tensor_values[idx++] = resized_img.at<cv::Vec3f>(h, w)[c];
                }
            }
        } 
        return { m_input_tensor_values, resized_img};
    }
    #endif

    // postprocess
    inline std::string  
    postprocess( float*& output_tensor
               , std::vector<int64_t>& output_shape
               , const std::any& additional_args) override {
        m_sequence_preds.clear();
        int sequence_length = output_shape[1];
        int num_classes = output_shape[2];

        #ifdef ENABLE_EIGEN
        Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            output_eigen(output_tensor, sequence_length, num_classes);

        for(int t = 0; t < sequence_length; ++t) {
            Eigen::Index max_idx;
            output_eigen.row(t).maxCoeff(&max_idx);
            m_sequence_preds.push_back(static_cast<int>(max_idx));
        } 
        #else
        for (int t = 0; t < sequence_length; ++t) {
            float max_prob = -1.f;
            int max_idx = -1;

            for (int c = 0; c < num_classes; ++c) {
                float prob = output_tensor[t * num_classes + c];
                if (prob > max_prob) {
                    max_prob = prob;
                    max_idx = c;
                }
            }
            m_sequence_preds.push_back(max_idx);
        }
        #endif

        m_result_cache.clear();
        int last_char_idx = -1;
        
        for (int idx: m_sequence_preds) {
            if (idx != last_char_idx) {
                int adjusted_idx = idx - 1;
                if (adjusted_idx >= 0 && adjusted_idx < m_char_dict.size()) {
                    std::string current_char = m_char_dict[adjusted_idx];
                    if (current_char != "■" && current_char != "<blank>" && current_char != " ") {
                        m_result_cache.push_back(current_char);
                    }
                    last_char_idx = idx;
                }
            }
        }

        std::string final_str;
        for (const auto& c: m_result_cache) final_str += c;
        return final_str;
   }

    inline std::string
    infer(cv::Mat& frame) override {
        //try {
            auto start_preprocess = std::chrono::high_resolution_clock::now();

            cv::Mat resized_img;
            
            #ifdef ENABLE_EIGEN
            std::tie(m_input_tensor_values, resized_img) = preprocess_eigen(frame);  
            #else
            std::tie(m_input_tensor_values, resized_img) = preprocess(frame);
            #endif

            auto end_preprocess = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_preprocess - start_preprocess);
            
            

            // 准备输入
            std::vector<int64_t> input_shape = {1, 3, resized_img.rows, resized_img.cols};
            
            // 检查形状是否变化，如果变化则需要重新创建tensor
            bool shape_changed = (m_last_input_shape != input_shape);
            if (shape_changed) {
                m_last_input_shape = input_shape;
            }

            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                m_memory_info, m_input_tensor_values.data(), m_input_tensor_values.size(),
                input_shape.data(), input_shape.size()
            );

            const char* input_names[] = {m_input_name.c_str()};
            const char* output_names[] = {m_output_name.c_str()};

            std::vector<Ort::Value> outputs = m_session.Run(
                Ort::RunOptions{nullptr},
                input_names, &input_tensor, 1,
                output_names, 1
            );

            auto output_tensor = outputs[0].GetTensorMutableData<float>();
            auto output_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    
            return postprocess(output_tensor, output_shape, {});
//         } catch (const Ort::Exception& e) {
//
//         } catch (const std::exception& e) {
//                    }
//         return {};
//     }
    }
    
public:
    rec_inferer(const std::string& model_path = "rec_server.onnx")
        : common_inferer<cv::Mat, std::string>(model_path)
        { load_chars("inf_src/classes/chars.txt"); }

    std::string run_inf(cv::Mat& frame) { return infer(frame); }
    void set_char_dict(const std::string& char_dict_path) { }
};

#endif // ORT_INF_HPP
