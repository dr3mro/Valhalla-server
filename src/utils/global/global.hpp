#define CRITICALMESSAGE                                                                                             \
    std::string critical_error = fmt::format("Failure: {} : {} : {} : {}", e.what(), __func__, __FILE__, __LINE__); \
    Message::CriticalMessage(critical_error);

#define CRITICALMESSAGERESPONSE \
    CRITICALMESSAGE             \
    callback(api::v2::Http::Status::INTERNAL_SERVER_ERROR, critical_error);
