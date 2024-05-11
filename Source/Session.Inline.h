#pragma once
#include "Session.h"

namespace Http {
inline CSession::CSession(std::shared_ptr<CServer>& server, asio::ip::tcp::socket&& socket, std::shared_ptr<std::string const> const& docRoot)
    : mServer(server)
    , mStream(std::move(socket))
    , mDocRoot(docRoot) {
    static_assert(KQueueLimit > 0, "queue limit must be positive");
}
inline void CSession::onRead(beast::error_code errorCode, std::size_t bytesTransferred) {
    boost::ignore_unused(bytesTransferred);

    if (errorCode == beast::http::error::end_of_stream)
        return doClose();

    if (errorCode) {
        BOOST_LOG_TRIVIAL(error) << "onRead(...): " << errorCode.what();
        return;
    }
    mServer->route(this, mRequestParser->release());
    // queueWrite(handleRequest(*mDocRoot, mRequestParser->release()));

    if (mResponseQueue.size() < KQueueLimit)
        doRead();
}
} // namespace Http
