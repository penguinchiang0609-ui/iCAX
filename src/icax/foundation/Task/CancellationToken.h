#pragma once

#include "TaskScheduler.h"
#include "TaskStatus.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace iCAX::Tasks
{
    namespace detail
    {
        struct CancellationCallbackEntry final
        {
            explicit CancellationCallbackEntry(std::function<void()> callback_)
                : callback(std::move(callback_))
            {
            }

            std::function<void()> callback;
            std::atomic_bool active = true;
        };

        struct CancellationState final
        {
            mutable std::mutex mutex;
            bool requested = false;
            bool disposed = false;
            std::size_t cancelAfterVersion = 0;
            std::vector<std::shared_ptr<CancellationCallbackEntry>> callbacks;
        };
    }

    class CancellationTokenRegistration final
    {
    public:
        CancellationTokenRegistration() = default;

        CancellationTokenRegistration(
            std::weak_ptr<detail::CancellationState> state_,
            std::shared_ptr<detail::CancellationCallbackEntry> entry_)
            : m_state(std::move(state_))
            , m_entry(std::move(entry_))
        {
        }

        CancellationTokenRegistration(const CancellationTokenRegistration&) = delete;
        CancellationTokenRegistration& operator=(const CancellationTokenRegistration&) = delete;

        CancellationTokenRegistration(CancellationTokenRegistration&& other_) noexcept
            : m_state(std::move(other_.m_state))
            , m_entry(std::move(other_.m_entry))
        {
        }

        CancellationTokenRegistration& operator=(CancellationTokenRegistration&& other_) noexcept
        {
            if (this != &other_)
            {
                Unregister();
                m_state = std::move(other_.m_state);
                m_entry = std::move(other_.m_entry);
            }

            return *this;
        }

        ~CancellationTokenRegistration()
        {
            Unregister();
        }

        void Unregister()
        {
            if (m_entry)
            {
                m_entry->active.store(false);
            }

            m_entry.reset();
            m_state.reset();
        }

    private:
        std::weak_ptr<detail::CancellationState> m_state;
        std::shared_ptr<detail::CancellationCallbackEntry> m_entry;
    };

    class CancellationToken final
    {
    public:
        CancellationToken() = default;

        bool CanBeCanceled() const noexcept
        {
            return m_state != nullptr;
        }

        bool IsCancellationRequested() const
        {
            if (!m_state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(m_state->mutex);
            return m_state->requested;
        }

        void ThrowIfCancellationRequested() const
        {
            if (IsCancellationRequested())
            {
                throw TaskCanceledException();
            }
        }

        CancellationTokenRegistration Register(std::function<void()> callback_) const
        {
            if (!m_state || !callback_)
            {
                return {};
            }

            auto entry = std::make_shared<detail::CancellationCallbackEntry>(std::move(callback_));
            bool runNow = false;
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                if (m_state->disposed)
                {
                    return {};
                }

                if (m_state->requested)
                {
                    runNow = true;
                }
                else
                {
                    m_state->callbacks.push_back(entry);
                }
            }

            if (runNow && entry->active.load())
            {
                entry->callback();
            }

            return CancellationTokenRegistration(m_state, entry);
        }

        static CancellationToken None() noexcept
        {
            return {};
        }

    private:
        explicit CancellationToken(std::shared_ptr<detail::CancellationState> state_)
            : m_state(std::move(state_))
        {
        }

    private:
        std::shared_ptr<detail::CancellationState> m_state;

        friend class CancellationTokenSource;
    };

    class CancellationTokenSource final
    {
    public:
        CancellationTokenSource()
            : m_state(std::make_shared<detail::CancellationState>())
            , m_linkedRegistrations(std::make_shared<std::vector<CancellationTokenRegistration>>())
        {
        }

        CancellationToken Token() const
        {
            return CancellationToken(m_state);
        }

        bool IsCancellationRequested() const
        {
            return Token().IsCancellationRequested();
        }

        bool Cancel() const
        {
            std::vector<std::shared_ptr<detail::CancellationCallbackEntry>> callbacks;
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                if (m_state->disposed || m_state->requested)
                {
                    return false;
                }

                m_state->requested = true;
                callbacks.swap(m_state->callbacks);
            }

            for (auto& entry : callbacks)
            {
                if (entry && entry->active.load() && entry->callback)
                {
                    entry->callback();
                }
            }

            return true;
        }

        void Dispose() const
        {
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                m_state->disposed = true;
                ++m_state->cancelAfterVersion;
                m_state->callbacks.clear();
            }

            for (auto& registration : *m_linkedRegistrations)
            {
                registration.Unregister();
            }
            m_linkedRegistrations->clear();
        }

        template<typename Rep, typename Period>
        void CancelAfter(const std::chrono::duration<Rep, Period>& timeout_) const
        {
            auto source = *this;
            std::size_t version = 0;
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                if (m_state->disposed || m_state->requested)
                {
                    return;
                }

                version = ++m_state->cancelAfterVersion;
            }

            auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_);
            if (delay < std::chrono::milliseconds::zero())
            {
                return;
            }

            ScheduleTimer(delay, [source, version]
            {
                {
                    std::lock_guard<std::mutex> lock(source.m_state->mutex);
                    if (source.m_state->disposed ||
                        source.m_state->requested ||
                        source.m_state->cancelAfterVersion != version)
                    {
                        return;
                    }
                }

                source.Cancel();
            });
        }

        static CancellationTokenSource CreateLinkedTokenSource(const std::vector<CancellationToken>& tokens_)
        {
            CancellationTokenSource source;
            for (const auto& token : tokens_)
            {
                if (!token.CanBeCanceled())
                {
                    continue;
                }

                source.m_linkedRegistrations->push_back(token.Register([source]
                {
                    source.Cancel();
                }));
            }

            return source;
        }

        template<typename... Tokens>
        static CancellationTokenSource CreateLinkedTokenSource(const Tokens&... tokens_)
        {
            return CreateLinkedTokenSource(std::vector<CancellationToken>{ tokens_... });
        }

    private:
        std::shared_ptr<detail::CancellationState> m_state;
        std::shared_ptr<std::vector<CancellationTokenRegistration>> m_linkedRegistrations;
    };
}
