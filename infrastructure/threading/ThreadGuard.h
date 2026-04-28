#pragma once
#include <qthread.h>
#include <qobject.h>
#include <atomic>

class ThreadGuard {
public:
    ThreadGuard() = default;
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

    template<typename Worker>
    void launch(Worker&& worker) {
        m_running = true;
        worker->moveToThread(&m_thread);
        QObject::connect(&m_thread, &QThread::started, worker.get(), [this] {
            m_running = true;
        });
        QObject::connect(&m_thread, &QThread::finished, worker.get(), [this] {
            m_running = false;
        });
        m_worker = std::move(worker);
        m_thread.start();
    }

    ~ThreadGuard() {
        stop();
    }

    void stop() {
        m_running = false;
        if (m_thread.isRunning()) {
            m_thread.quit();
            m_thread.wait(5000);
        }
    }

    bool isRunning() const { return m_running; }
    QThread* thread() { return &m_thread; }

private:
    QThread m_thread;
    std::atomic<bool> m_running{false};
    std::shared_ptr<QObject> m_worker;
};
