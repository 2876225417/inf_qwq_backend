#ifndef CHARS_ORT_INFERER
#define CHARS_ORT_INFERER

#include <algorithm>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <future>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <chrono>
#include <iostream>
#include <inferer/ort_inf.hpp>

class ResultNotifier;

struct inference_task {
    int cam_id;
    cv::Mat image;

    inference_task(int id, const cv::Mat& img): cam_id{id}, image{img} {}
    inference_task(): cam_id{-1}{}
};

struct inference_result {
    int cam_id;
    std::vector<std::string> texts;
    inference_result(): cam_id(-1) {}
    inference_result(int id, const std::vector<std::string>& results)
    : cam_id{id}
    , texts{results} { }
};

template<typename T>
class ThreadSafeQueue {
private:
    mutable std::mutex m_mutex;
    std::queue<T> m_queue;
    std::condition_variable m_cond;
    std::atomic<bool> m_done{false};

public:
    ThreadSafeQueue() = default;

    void push(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(value));
        m_cond.notify_one();
    }

    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return false;
        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    bool wait_and_pop(T& value) {
        std::unique_lock<std::mutex> lock(m_mutex);
    
        if (!m_cond.wait_for(lock, std::chrono::milliseconds(1), [this]{
            return !m_queue.empty() || m_done;
        })) {
            return false; 
        }

        if (m_queue.empty()) return false;
        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    void done() {
        m_done = true;
        m_cond.notify_all();
    }

    bool is_done() const {
        return m_done;
    }
};

class TaskProcessor {
private:
    ThreadSafeQueue<inference_task> m_task_queue;
    ThreadSafeQueue<inference_result> m_result_queue;
    std::vector<std::thread> m_threads;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_active_tasks{0};
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::function<void()> m_result_callback;

    std::vector<std::unique_ptr<det_inferer>> m_det_inferers;
    std::vector<std::unique_ptr<rec_inferer>> m_rec_inferers;

public:
    TaskProcessor(int num_threads, const std::string& det_model_path, const std::string& rec_model_path) {
        try {
            for (int i = 0; i < num_threads; i++) {
                m_det_inferers.push_back(std::make_unique<det_inferer>(det_model_path));
                m_rec_inferers.push_back(std::make_unique<rec_inferer>(rec_model_path));
            }

            for (int i = 0; i < num_threads; i++) {
                m_threads.emplace_back(&TaskProcessor::worker_thread, this, i);
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize task processor: " << e.what() << std::endl;
            throw;
        }
    }

    ~TaskProcessor() {
        stop();
    }

    void add_task(inference_task task) {
        m_task_queue.push(std::move(task));
        m_active_tasks++;
    }

    bool get_result(inference_result& result) {
        return m_result_queue.try_pop(result);
    }

    void set_result_callback(std::function<void()> callback) {
        m_result_callback = std::move(callback);
    }

    void stop() {
        m_stop = true;
        m_task_queue.done();
        for (auto& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    bool is_idle() const {
        return m_active_tasks.load() == 0 && m_task_queue.empty();
    }

    int pending_tasks() const {
        return m_active_tasks.load();
    }

private:
    void notify_result_available() {
        if (m_result_callback) {
            m_result_callback();
        }
    }

    void worker_thread(int thread_id) {
        std::cout << "Worker thread " << thread_id << " started" << std::endl;

        det_inferer* det = m_det_inferers[thread_id].get();
        rec_inferer* rec = m_rec_inferers[thread_id].get();

        inference_task task;
        while (!m_stop) {
            bool got_task = m_task_queue.wait_and_pop(task);
            if (!got_task) {
                if (m_stop) break;

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            try {
                std::vector<cv::Mat> detected_regions = det->run_inf(task.image);

                std::vector<std::string> result_set;
                for (auto& region : detected_regions) {
                    std::string recognized_text = rec->run_inf(region);
                    if (!recognized_text.empty()) {
                        result_set.push_back(recognized_text);
                        //std::cout << "rec res: " << recognized_text << std::endl;
                    }
                }

                m_result_queue.push(inference_result(task.cam_id, result_set));

                notify_result_available();
            } catch (const std::exception& e) {
                std::cerr << "Error in worker thread " << thread_id << ": " << e.what() << std::endl;
                m_result_queue.push(inference_result(task.cam_id, {}));
                notify_result_available();
            }

            m_active_tasks--;
            m_cond.notify_all();
        }

        std::cout << "Worker thread " << thread_id << " stopped" << std::endl;
    }
};

class ResultNotifier {
public:
    ResultNotifier() = default;

    void set_callback(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callback = std::move(callback);
    }

    void notify() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_callback) {
            m_callback();
        }
    }

private:
    std::mutex m_mutex;
    std::function<void()> m_callback;
};

class chars_ort_inferer {
public:
    chars_ort_inferer(const std::string& det_model_path = "det_gen.onnx",
                      const std::string& rec_model_path = "rec_gen.onnx",
                      int num_threads = 16)
    : m_processor{new TaskProcessor(num_threads, det_model_path, rec_model_path)}
    , m_notifier{new ResultNotifier()}
    , m_stop{false}
    {
        m_notifier->set_callback([this]() {
            check_results();
        });

        m_processor->set_result_callback([this]() {
            m_notifier->notify();
        });

        m_result_check_thread = std::thread([this]() {
            result_check_loop();
        });
    }

    std::function<void(int, const std::vector<std::string>&)> get_completion_callback() const {
        return m_completion_callback;
    }


    ~chars_ort_inferer() {
        m_stop = true;
        if (m_result_check_thread.joinable()) {
            m_result_check_thread.join();
        }
        if (m_processor) {
            delete m_processor;
        }
        if (m_notifier) {
            delete m_notifier;
        }
    }

    void run_inf(int cam_id, const cv::Mat& frame) {
        if (frame.empty()) {
            std::cerr << "Invalid input frame" << std::endl;
            return;
        }

        cv::Mat frameCopy = frame.clone();
        m_processor->add_task(inference_task(cam_id, frameCopy));
    }

    void run_inf_batch(const std::vector<std::pair<int, cv::Mat>>& frames) {
        for (const auto& [cam_id, frame] : frames) {
            if (!frame.empty()) {
                cv::Mat frameCopy = frame.clone();
                m_processor->add_task(inference_task(cam_id, frameCopy));
            }
        }
    }

    void wait_for_completion(int timeout_ms = -1) {
        auto start = std::chrono::steady_clock::now();
        while (!m_processor->is_idle()) {
            check_results(); 

            if (timeout_ms > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                    if (elapsed > timeout_ms) {
                        std::cerr << "Wait for completion timed out after " << timeout_ms << " ms" << std::endl;
                        break;
                    }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void set_completion_callback(std::function<void(int, const std::vector<std::string>&)> callback) {
        std::lock_guard<std::mutex> lock(m_callback_mutex);
        m_completion_callback = std::move(callback);
    }

private:
    void check_results() {
        inference_result result;
        int processed_count = 0;
        const int MAX_PROCESS_PER_CALL = 10; 

        while (processed_count < MAX_PROCESS_PER_CALL && m_processor->get_result(result)) {
            std::lock_guard<std::mutex> lock(m_callback_mutex);
            if (m_completion_callback) {
                m_completion_callback(result.cam_id, result.texts);
                std::cout << "cam id: " << result.cam_id  
                          << "results: ";
                for(auto& text_result: result.texts)
                    std::cout << text_result << ' ';
                std::cout << std::endl;
            }
            processed_count++;
        }
    }

    void result_check_loop() {
        while (!m_stop) {
            check_results();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

private:
    TaskProcessor* m_processor;
    ResultNotifier* m_notifier;
    std::thread m_result_check_thread;
    std::atomic<bool> m_stop;

    std::mutex m_callback_mutex;
    std::function<void(int, const std::vector<std::string>&)> m_completion_callback;
};

#endif
