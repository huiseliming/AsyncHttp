#include "HttpUtils.h"

namespace Http {
    beast::string_view MimeType(beast::string_view path)
    {
        using beast::iequals;
        auto const ext = [&path]
            {
                auto const pos = path.rfind(".");
                if (pos == beast::string_view::npos)
                    return beast::string_view{};
                return path.substr(pos);
            }();
        if (iequals(ext, ".htm"))  return "text/html";
        if (iequals(ext, ".html")) return "text/html";
        if (iequals(ext, ".php"))  return "text/html";
        if (iequals(ext, ".css"))  return "text/css";
        if (iequals(ext, ".txt"))  return "text/plain";
        if (iequals(ext, ".js"))   return "application/javascript";
        if (iequals(ext, ".json")) return "application/json";
        if (iequals(ext, ".xml"))  return "application/xml";
        if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
        if (iequals(ext, ".flv"))  return "video/x-flv";
        if (iequals(ext, ".png"))  return "image/png";
        if (iequals(ext, ".jpe"))  return "image/jpeg";
        if (iequals(ext, ".jpeg")) return "image/jpeg";
        if (iequals(ext, ".jpg"))  return "image/jpeg";
        if (iequals(ext, ".gif"))  return "image/gif";
        if (iequals(ext, ".bmp"))  return "image/bmp";
        if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
        if (iequals(ext, ".tiff")) return "image/tiff";
        if (iequals(ext, ".tif"))  return "image/tiff";
        if (iequals(ext, ".svg"))  return "image/svg+xml";
        if (iequals(ext, ".svgz")) return "image/svg+xml";
        return "application/text";
    }
    std::string PathCat(beast::string_view base, beast::string_view path) {
        if (base.empty())
            return std::string(path);
        std::string result(base);
#ifdef BOOST_MSVC
        char constexpr pathSeparator = '\\';
        if (result.back() == pathSeparator)
            result.resize(result.size() - 1);
        result.append(path.data(), path.size());
        for (auto& c : result)
            if (c == '/')
                c = pathSeparator;
#else
        char constexpr pathSeparator = '/';
        if (result.back() == pathSeparator)
            result.resize(result.size() - 1);
        result.append(path.data(), path.size());
#endif
        return result;
    }
    beast::http::response<beast::http::string_body> BadRequest(beast::http::request<beast::http::string_body>&& req, std::string body)
    {
        req.target();
        beast::http::response<beast::http::string_body> res{ beast::http::status::bad_request, req.version() };
        res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(body);
        res.prepare_payload();
        return res;
    }
    beast::http::response<beast::http::string_body> NotFound(beast::http::request<beast::http::string_body>&& req, std::string body)
    {
        beast::http::response<beast::http::string_body> res{ beast::http::status::not_found, req.version() };
        res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(body);
        res.prepare_payload();
        return res;
    }
    beast::http::response<beast::http::string_body> InternalServerError(beast::http::request<beast::http::string_body>&& req, std::string body)
    {
        beast::http::response<beast::http::string_body> res{ beast::http::status::internal_server_error, req.version() };
        res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(body);
        res.prepare_payload();
        return res;
    }
    //beast::http::response<beast::http::string_body> InternalServerError(unsigned int version, std::string&& body)
    //{
    //    beast::http::response<beast::http::string_body> res{ beast::http::status::internal_server_error, version };
    //    res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    //    res.set(beast::http::field::content_type, "text/html");
    //    res.keep_alive(false);
    //    res.body() = std::move(body);
    //    res.prepare_payload();
    //    return res;
    //}
}
