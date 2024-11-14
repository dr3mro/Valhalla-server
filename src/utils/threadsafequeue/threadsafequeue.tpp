

template <typename T>
void ThreadSafeQueue<T>::push(const T& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(value);
    cv_.notify_one();
}

template <typename T>
void ThreadSafeQueue<T>::push(T&& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(value));
    cv_.notify_one();
}

template <typename T>
std::optional<T> ThreadSafeQueue<T>::pop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty())
    {
        return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop();
    return value;
}

template <typename T>
bool ThreadSafeQueue<T>::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

template <typename T>
size_t ThreadSafeQueue<T>::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

template <typename T>
void ThreadSafeQueue<T>::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<T>               empty;
    std::swap(queue_, empty);
}

template <typename T>
template <typename... Args>
void ThreadSafeQueue<T>::emplace(Args&&... args)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.emplace(std::forward<Args>(args)...);
    cv_.notify_one();
}

template <typename T>
std::optional<T> ThreadSafeQueue<T>::front()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
    {
        return std::nullopt;
    }
    return queue_.front();
}