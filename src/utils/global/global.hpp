#define CRITICALMESSAGE                                                                                             \
    std::string critical_error = fmt::format("Failure: {} : {} : {} : {}", e.what(), __func__, __FILE__, __LINE__); \
    Message::CriticalMessage(critical_error);
#define CRITICALMESSAGERESPONSE \
    CRITICALMESSAGE             \
    callback(500, critical_error);