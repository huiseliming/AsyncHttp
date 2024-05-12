#pragma once
#include <boost/mysql.hpp>
#include <boost/log/trivial.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

namespace MySQL {
using namespace boost;

class CDBConnectionPool;

class CPooledConnection : public std::enable_shared_from_this<CPooledConnection> {
  public:
    CPooledConnection(CDBConnectionPool* dbConnectionPool, mysql::tcp_ssl_connection* connection);
    CPooledConnection(const CPooledConnection&) = delete;
    CPooledConnection& operator=(const CPooledConnection&) = delete;
    CPooledConnection(CPooledConnection&&);
    CPooledConnection& operator=(CPooledConnection&&);

    virtual ~CPooledConnection();

    mysql::tcp_ssl_connection* getConnection() { return mSSLConnection; }

  protected:
    CDBConnectionPool* mDBConnectionPool;
    mysql::tcp_ssl_connection* mSSLConnection;
};

class CDBConnectionPool {
    constexpr static size_t KNumConnections = 8;
  public:
    CDBConnectionPool(asio::io_context& ioContext, const std::string& username, const std::string& password, asio::ip::address address = asio::ip::address_v4::loopback(), uint16_t port = 3306)
        : mIoContext(ioContext)
        , mStrand(ioContext)
        , mSSLContext(asio::ssl::context::tls_client)
        , mAvailableConnections({})
        , mUsername(username)
        , mPassword(password)
        , mAddress(address)
        , mPort(port) {
    }

    void asyncGetConnection(std::function<void(CPooledConnection pooledConnection)> completeCallback) {
        asio::dispatch(mStrand, [=, completeCallback = std::move(completeCallback)] {
            auto connection = allocConnection();
            if (connection) {
                completeCallback(CPooledConnection(this, connection));
            } else {
                mAsyncCallbackQueue.push(completeCallback);
            }
        });
    }

    //void asyncQuery(std::string_view sql) {
    //    std::unique_ptr<mysql::tcp_ssl_connection> connection = allocConnection();
    //    if (connection) {
    //        connection->async_query("",);
    //    } else {

    //    }
    //}

    int64_t prepareStatement(std::string_view sql) {}
    int64_t executeStatement(int64_t statementIndex) {}
    int64_t executeStatement() {}

    //asio::io_context& ioContext() { return mIoContext; }
    //asio::ssl::context& sslContext() { return mSSLContext; }
    //const std::string& username() { return mUsername; }
    //const std::string& password() { return mPassword; }
    //const asio::ip::address& address() { return mAddress; }
    //uint16_t port() { return mPort; }

    mysql::tcp_ssl_connection* allocConnection() {
        mysql::tcp_ssl_connection* connection = nullptr;
        if (!mAvailableConnections.empty()) {
            connection = mAvailableConnections.back();
            mAvailableConnections.pop_back();
        } else {
            if (mAvailableConnections.size() < mNumConnections) {
                mNumConnections++;
                asio::ip::tcp::resolver* resolver = new asio::ip::tcp::resolver(mIoContext.get_executor());
                resolver->async_resolve(asio::ip::tcp::endpoint(mAddress, mPort), [=](const boost::system::error_code& errorCode, asio::ip::tcp::resolver::results_type endpoints) {
                    delete resolver;
                    if (errorCode.failed()) {
                        asio::post(mStrand, [=] { mNumConnections--; });
                        return;
                    }
                    mysql::handshake_params handshakeParams(mUsername, mPassword);
                    std::shared_ptr<boost::mysql::diagnostics> diagnostics = std::make_shared<boost::mysql::diagnostics>();
                    mysql::tcp_ssl_connection* connection = new mysql::tcp_ssl_connection(mIoContext, mSSLContext);
                    connection->async_connect(*endpoints.begin(), handshakeParams, *diagnostics, [=](mysql::error_code errorCode) {
                        if (errorCode) {
                            asio::post(mStrand, [=] { mNumConnections--; });
                            BOOST_LOG_TRIVIAL(error) << "MYSQL client message: " << diagnostics->client_message();
                            BOOST_LOG_TRIVIAL(error) << "MYSQL server message: " << diagnostics->server_message();
                            delete connection;
                            return;
                        }
                        freeConnection(connection);
                    });
                });
            }
        }
        return connection;
    }

    void freeConnection(mysql::tcp_ssl_connection* connection) { 
        asio::dispatch(mStrand, [=, connection = std::move(connection)] () mutable { 
            if (!mAsyncCallbackQueue.empty()) {
                auto asyncCallback = std::move(mAsyncCallbackQueue.back());
                mAsyncCallbackQueue.pop();
                asyncCallback(CPooledConnection(this, std::move(connection)));
            } else {
                mAvailableConnections.emplace_back(std::move(connection));
            }
        });
    }

  protected:
    asio::io_context& mIoContext;
    asio::io_context::strand mStrand;
    asio::ssl::context mSSLContext;
    std::string mUsername;
    std::string mPassword;
    asio::ip::address mAddress;
    uint16_t mPort;
    size_t mNumConnections;
    std::vector<mysql::tcp_ssl_connection*> mAvailableConnections;
    std::queue<std::function<void(CPooledConnection)>> mAsyncCallbackQueue;
};

inline CPooledConnection::CPooledConnection(CDBConnectionPool* dbConnectionPool, mysql::tcp_ssl_connection* connection)
    : mDBConnectionPool(dbConnectionPool)
    , mSSLConnection(std::move(connection)) {}

inline CPooledConnection::CPooledConnection(CPooledConnection&& other)
    : mDBConnectionPool(other.mDBConnectionPool)
    , mSSLConnection(std::move(other.mSSLConnection)) {}

CPooledConnection& CPooledConnection::operator=(CPooledConnection&& other) {
    mDBConnectionPool = other.mDBConnectionPool;
    mSSLConnection = std::move(other.mSSLConnection);
    return *this;
}

inline CPooledConnection::~CPooledConnection() {
    if (mSSLConnection) {
        mDBConnectionPool->freeConnection(mSSLConnection);
    }
}
//
//bool isFatalError(boost::mysql::error_code errorCode) noexcept {
//    // If there is no failure, it's not fatal
//    if (!errorCode)
//        return false;
//
//    // Retrieve the error category
//    const auto& cat = errorCode.category();
//
//    if (cat == boost::mysql::get_common_server_category()) {
//        // Server errors may or may not be fatal. MySQL defines a ton of different errors.
//        // After some research, these are the ones I'd recommend to consider fatal
//        auto code = static_cast<boost::mysql::common_server_errc>(ec.value());
//        switch (code) {
//        // Diferent flavors of communication errors. These usually indicate that the connection
//        // has been left in an unspecified state, and the safest is to reconnect it.
//        case boost::mysql::common_server_errc::er_unknown_com_error:
//        case boost::mysql::common_server_errc::er_aborting_connection:
//        case boost::mysql::common_server_errc::er_net_packet_too_large:
//        case boost::mysql::common_server_errc::er_net_read_error_from_pipe:
//        case boost::mysql::common_server_errc::er_net_fcntl_error:
//        case boost::mysql::common_server_errc::er_net_packets_out_of_order:
//        case boost::mysql::common_server_errc::er_net_uncompress_error:
//        case boost::mysql::common_server_errc::er_net_read_error:
//        case boost::mysql::common_server_errc::er_net_read_interrupted:
//        case boost::mysql::common_server_errc::er_net_error_on_write:
//        case boost::mysql::common_server_errc::er_net_write_interrupted:
//        case boost::mysql::common_server_errc::er_malformed_packet:
//        // This one indicates that you are preparing statements dynamically and
//        // never calling statement::close or connection::reset_connection.
//        // Restarting the connection will clean up any leaked statements.
//        // It's recommended that you design your code so this never happens.
//        // But better safe than sorry.
//        case boost::mysql::common_server_errc::er_max_prepared_stmt_count_reached:
//            return true;
//        default:
//            return false;
//        }
//    } else if (cat == boost::mysql::get_mysql_server_category() || cat == boost::mysql::get_mariadb_server_category()) {
//        // This is a MySQL-specific or a MariaDB specific error. They are all
//        // considered non-fatal. If you're working with a specific DB system,
//        // you can comment the one you're not using
//        return false;
//    } else if (errorCode == boost::mysql::client_errc::wrong_num_params) {
//        // Errors in the boost::mysql::get_client_category() are all fatal
//        // except for this one.
//        return false;
//    } else {
//        // Errors in any all category are considered fatal. They will usually
//        // be asio or SSL related errors. The only option here is to reconnect.
//        return true;
//    }
//}

//class CConnection : std::enable_shared_from_this<CConnection> {
//  public:
//    CConnection(asio::io_context& ioContext, const std::string& username, const std::string& password, asio::ip::address address = asio::ip::address_v4::loopback(), uint16_t port = 3306)
//        : mIoContext(ioContext)
//        , mSSLContext(asio::ssl::context::tls_client)
//        , mSSLConnection(ioContext, mSSLContext)
//        , mUsername(username)
//        , mPassword(password)
//        , mAddress(address)
//        , mPort(port) {}
//    virtual ~CConnection() {}
//
//    void doConnect() {
//        asio::ip::tcp::resolver resolver(mIoContext.get_executor());
//        resolver.async_resolve(asio::ip::tcp::endpoint(mAddress, mPort), [=, self = shared_from_this()](const boost::system::error_code& errorCode, asio::ip::tcp::resolver::results_type endpoints) {
//            if (errorCode.failed()) {
//                return;
//            }
//            mysql::handshake_params handshakeParams(mUsername, mPassword);
//            std::shared_ptr<boost::mysql::diagnostics> diagnostics = std::make_shared<boost::mysql::diagnostics>();
//            mSSLConnection.async_connect(*endpoints.begin(), handshakeParams, *diagnostics, [=, self = std::move(self)](mysql::error_code errorCode) {
//                if (errorCode) {
//                    BOOST_LOG_TRIVIAL(error) << "MYSQL client message: " << diagnostics->client_message();
//                    BOOST_LOG_TRIVIAL(error) << "MYSQL server message: " << diagnostics->server_message();
//                    return;
//                }
//            });
//        });
//    }
//
//  protected:
//    asio::io_context& mIoContext;
//    asio::ssl::context mSSLContext;
//    mysql::tcp_ssl_connection mSSLConnection;
//    std::string mUsername;
//    std::string mPassword;
//    asio::ip::address mAddress;
//    uint16_t mPort;
//    std::queue<std::function<void()>> mSQLQueue;
//};

} // namespace MySQL
