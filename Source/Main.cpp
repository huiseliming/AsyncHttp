
#include "Http.h"
#include "HttpMySQL.h"

#include <boost/mysql/static_results.hpp>
#include <boost/describe/class.hpp>
#define RAPIDJSON_HAS_STDSTRING 1
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace boost;

struct Tester {
    Tester() { 
        BOOST_LOG_TRIVIAL(error) << " Tester()"; 
    }
    ~Tester() { 
        BOOST_LOG_TRIVIAL(error) << " ~Tester()  "; 
    }
    int cc[16] = {0};
};


template <typename CompletionToken>
auto f2(int i, CompletionToken&& token) {
    return asio::async_compose<CompletionToken, void()>([=](auto&& self) mutable{
        std::thread([=, self = std::move(self)]() mutable { 
                
            std::this_thread::sleep_for(std::chrono::seconds(3));
            BOOST_LOG_TRIVIAL(error) << "co: " << i;
            self.complete();
        }).detach();
    }, token);
}

boost::asio::awaitable<void> echo(int i) {
    co_await f2(i, asio::use_awaitable);
    BOOST_LOG_TRIVIAL(error) << "echo: i" << i;
}

boost::asio::awaitable<void> Sql(MySQL::CDBConnectionPool* connectionPool) {
    MySQL::CPooledConnection pooledConnection = co_await connectionPool->asyncAllocConnection(asio::use_awaitable);
    if (pooledConnection.getConnection()) {
        boost::mysql::results result;
        co_await pooledConnection.getConnection()->async_query("SHOW DATABASES", result, asio::use_awaitable);
        BOOST_LOG_TRIVIAL(error) << " results : " << result.rows().at(0).at(0) ;
    } else {
        BOOST_LOG_TRIVIAL(error) << " not connection : ";
    }
}

struct User {
    std::uint64_t id;
    std::string wxId;
    std::optional<std::string> username;
    std::optional<std::string> password;
    std::optional<std::string> nickname;
    std::optional<std::string> phoneNumber;
    std::optional<std::string> companyName;
    std::uint64_t totalConsumption;
};
BOOST_DESCRIBE_STRUCT(User, (), (id, wxId, username, password, nickname, phoneNumber, companyName, totalConsumption))

class CUserController {
  public:
    CUserController(MySQL::CDBConnectionPool& connectionPool, std::shared_ptr<Http::CServer> server)
        : mConnectionPool(connectionPool)
        , mServer(server) {}

    asio::awaitable<void> asyncGetUser(std::shared_ptr<Http::CSession> session, beast::http::request<beast::http::string_body> request, int64_t id) {
        try {
            mysql::error_code errorCode;
            mysql::diagnostics diagnostics;
            MySQL::CPooledConnection pooledConnection = co_await mConnectionPool.asyncAllocConnection(asio::use_awaitable);
            if (pooledConnection.getConnection()) {
                mysql::statement statement;
                std::tie(errorCode, statement) = co_await pooledConnection.getConnection()->async_prepare_statement("SELECT * FROM user WHERE id = ?", diagnostics, asio::as_tuple(boost::asio::use_awaitable));
                boost::mysql::throw_on_error(errorCode, diagnostics);
                mysql::static_results<User> userResult;
                std::tie(errorCode) = co_await pooledConnection.getConnection()->async_execute(statement.bind(id), userResult, diagnostics, asio::as_tuple(boost::asio::use_awaitable));
                boost::mysql::throw_on_error(errorCode, diagnostics);
                auto row = userResult.rows();
                if (!row.empty()) {
                    auto& user = row[0];
                    rapidjson::Document doc;
                    doc.SetObject();
                    doc.AddMember("id", user.id, doc.GetAllocator());
                    doc.AddMember("wxId", user.wxId, doc.GetAllocator());
                    //doc.AddMember("username", user.username., doc.GetAllocator());
                    //doc.AddMember("password", user.password, doc.GetAllocator());
                    //doc.AddMember("nickname", user.nickname, doc.GetAllocator());
                    //doc.AddMember("phoneNumber", user.phoneNumber, doc.GetAllocator());
                    //doc.AddMember("companyName", user.companyName, doc.GetAllocator());
                    doc.AddMember("totalConsumption", user.totalConsumption, doc.GetAllocator());
                    rapidjson::StringBuffer buffer;
                    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                    doc.Accept(writer);
                    session->sendResponse(Http::Ok(std::move(request), buffer.GetString()));
                } else {
                    session->sendResponse(Http::InternalServerError(std::move(request), "Not user"));
                }
            } else {
                session->sendResponse(Http::InternalServerError(std::move(request), "not connection"));
            }
        } catch (const mysql::error_with_diagnostics& mysqlError) {
            session->sendResponse(Http::InternalServerError(std::move(request), std::format("Error: {}\ndiagnostics: {}", mysqlError.what(), std::string_view(mysqlError.get_diagnostics().server_message()))));
        }
    }

    void getUser(Http::CSession* session, beast::http::request<beast::http::string_body>&& request, int64_t id) {
        //std::shared_ptr<beast::http::request<beast::http::string_body>> requestPtr = std::make_shared<beast::http::request<beast::http::string_body>>(std::move(request));
        asio::co_spawn(mServer->ioContext(), asyncGetUser(session->shared_from_this(), std::move(request), id), 
        [=](std::exception_ptr exception_ptr) {
            if (exception_ptr) {
                try {
                    std::rethrow_exception(exception_ptr);
                } catch (const mysql::error_with_diagnostics& mysqlError) {
                    //session->sendResponse(Http::InternalServerError(std::move(*requestPtr), std::format("Error: {}\ndiagnostics: {}", mysqlError.what(), std::string_view(mysqlError.get_diagnostics().server_message()))));
                }
            }
        });
    }

    MySQL::CDBConnectionPool& mConnectionPool;
    std::shared_ptr<Http::CServer> mServer;
};

int main(int argc, char* argv[]) {

    std::string_view ccs = "dsds";
    auto aa = boost::lexical_cast<std::string>(ccs);

    std::cout << typeid(std::tuple_element<0, boost::callable_traits::args_t<decltype(main)>>::type).name() << std::endl;
    // std::cout << typeid(boost::mpl::at_c<boost::function_types::parameter_types<decltype(main)>, 0>::type).name() << std::endl;
    // boost::mpl::at_c<boost::function_types::parameter_types<decltype(main)>, 0>::type;

    try {
        std::shared_ptr<Http::CServer> server = std::make_shared<Http::CServer>(boost::asio::ip::address_v4::any(), 80);
        //for (size_t i = 0; i < 64; i++) {
        //    asio::co_spawn(server->ioContext(), echo(i), boost::asio::detached);
        //}
        MySQL::CDBConnectionPool dbcp(server->ioContext(), "root", "", "user");
        //asio::co_spawn(server->ioContext(), Sql(&dbcp), boost::asio::detached);
        CUserController userController(dbcp, server);
        server->setEnabled(true);
        server->addRoute(beast::http::verb::get, "/bxzn_v1/user/{}", std::function<void (Http::CSession *, beast::http::request<beast::http::string_body> &&, int64_t)>(std::bind_front(&CUserController::getUser, &userController)));
        server->addRoute(beast::http::verb::get, "/hello/{}///{}/{}/{}/{}/{}", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, std::string a, std::string&& b, const std::string& c, int d, double&& e, const int64_t& f) {
            std::cout << "a: " << a << std::endl;
            std::cout << "b: " << b << std::endl;
            std::cout << "c: " << c << std::endl;
            std::cout << "d: " << d << std::endl;
            std::cout << "e: " << e << std::endl;
            std::cout << "f: " << f << std::endl;
            session->sendResponse(Http::InternalServerError(std::move(request), "world!"));
        });
        server->addRoute(beast::http::verb::get, "/hello/{}/world", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, std::string a) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        server->addRoute(beast::http::verb::get, "/hello/{}/world/{}", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>&) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        server->addRoute(beast::http::verb::get, "/h/e/l/l/o/{}", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>&) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        server->addRoute(beast::http::verb::get, "/h/e/l/l/o/{}/w/o/r/l/d", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>&, std::string a) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        std::this_thread::sleep_for(std::chrono::seconds(6000));
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "onWrite(...): " << e.what();
    }
    return 0;
}