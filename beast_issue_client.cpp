
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/asio/strand.hpp>
#include <boost/optional/optional.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <cstdio>
#include <iomanip>
#include <cinttypes>
#include <list>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

typedef std::chrono::system_clock Clock;
typedef std::chrono::duration<double,std::ratio<1,1>> Duration;


//------------------------------------------------------------------------------

std::string show_current_time()
{
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::stringstream output;

    output << std::put_time(std::localtime(&time), "%F %T");

    return output.str();
}


// Report a failure
void fail(beast::error_code ec, char const *what)
{
    std::cerr << show_current_time() << "|" << what << ": " << ec.message() << "\n";
}

// Sends a WebSocket message and prints the response
class WebsocketSession : public std::enable_shared_from_this<WebsocketSession>
{
    tcp::resolver resolver_;
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::string host_;
    std::string port_;

    std::list<std::string> sendMessageQueue_;

    bool hasClose_;

    int recvCount_; // count of received message
    bool isConnected_; // is websocket connection established

public:
    // Resolver and socket require an io_context
    explicit WebsocketSession(net::io_context &ioc) : resolver_(net::make_strand(ioc)), ws_(net::make_strand(ioc)),
                                                      hasClose_(false), recvCount_(0), isConnected_(false)
    {

    }

    // Start the asynchronous operation
    void run(char const *host, char const *port)
    {
        // Save these for later
        host_ = host;
        port_ = port;

        std::cout << "run start" << std::endl;

        // Look up the domain name
        resolver_.async_resolve(host, port, beast::bind_front_handler(&WebsocketSession::on_resolve, shared_from_this()));
    }

    void on_resolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        if (ec)
            return fail(ec, "resolve");

        // Set the timeout for the operation
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(ws_).async_connect(results,
                                                   beast::bind_front_handler(&WebsocketSession::on_connect, shared_from_this()));
    }


    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if (ec)
        {
            fail(ec, "connect");
            return;
        }

        ws_.text(true);

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(ws_).expires_never();
        ws_.read_message_max(1 * 1024 * 1024);

        websocket::stream_base::timeout opt{std::chrono::seconds(20),   // handshake timeout
                                            std::chrono::seconds(6),        // idle timeout
                                            true // ping when idle
        };
        ws_.set_option(opt);

        ws_.set_option(websocket::stream_base::decorator([](websocket::request_type &req)
                                                         {
                                                             req.set(http::field::user_agent,
                                                                     std::string(BOOST_BEAST_VERSION_STRING) +
                                                                     " websocket-client-async");
                                                         }));

        // Perform the websocket handshake
        ws_.async_handshake(host_, "/", beast::bind_front_handler(&WebsocketSession::on_handshake, shared_from_this()));
    }

    void on_handshake(beast::error_code ec)
    {
        if (ec)
            return fail(ec, "handshake");

        isConnected_ = true;
        //after handshake, async_read from peer
        buffer_.clear();
        ws_.async_read(buffer_, beast::bind_front_handler(&WebsocketSession::on_read, shared_from_this()));

    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
        {
            fail(ec, "read");

            if (ec == beast::error::timeout)
            {
                std::cout << show_current_time() << "|idle timeout when read" << std::endl;
            }
        }
        else
        {
            ++recvCount_;
            std::cout << show_current_time() << "|"
            << "recv:" << beast::make_printable(buffer_.cdata()) << " |total recv:" << recvCount_ << std::endl;
        }

        if(ws_.is_open())
        {
            buffer_.clear();
            ws_.async_read(buffer_, beast::bind_front_handler(&WebsocketSession::on_read, shared_from_this()));
        }
    }

    void sendMessage(const std::string &msg)
    {
        sendMessageQueue_.push_back(msg);

        if (sendMessageQueue_.size() <= 1)
        {
            auto &frontMsg = sendMessageQueue_.front();
            doSend(frontMsg);
        }

    }

    void doSend(const std::string &msg)
    {
        if (!ws_.is_open())
        {
            return;
        }

        ws_.async_write(net::buffer(msg), beast::bind_front_handler(&WebsocketSession::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        sendMessageQueue_.pop_front();

        if (ec)
        {
            fail(ec, "write");
            if(ec == beast::error::timeout)
            {
                std::cout << show_current_time() << "| idle timeout when write" << std::endl;
            }
        }

        if (!sendMessageQueue_.empty())
        {
            auto &frontMsg = sendMessageQueue_.front();
            doSend(frontMsg);
        }

    }

    int getRecvCount() const
    {
        return recvCount_;
    }

    bool isConnected() const
    {
        return isConnected_;
    }

    ~WebsocketSession()
    {
        std::cout << "WebsocketSession de construct" <<  std::endl;
    }


    void close()
    {
        //show_current_time("now close:");
        if (hasClose_)
        {
            return;
        }
        hasClose_ = true;
        ws_.async_close(websocket::close_code::normal,
                        beast::bind_front_handler(&WebsocketSession::on_close, shared_from_this()));
    }

    void on_close(beast::error_code ec)
    {
        if (ec)
            return fail(ec, "close");


        // The make_printable() function helps print a ConstBufferSequence
        std::cout << "close websocket" << std::endl;
    }

private:
};

enum Stage
{
    kStageInit,
    kStageConnecting,
    kStageConnected,
    kStageClosing
};


int main(int argc, char **argv)
{
    // Check command line arguments.
    if (argc < 3)
    {
        std::cerr << "Usage: ws_cli <host> <port>\n" << "Example:\n"
                  << "    ws_cli echo.websocket.org 80 ""\n";
        return EXIT_FAILURE;
    }
    auto const host = argv[1];
    auto const port = argv[2];

    // The io_context is required for all I/O
    net::io_context ioc;

    net::executor_work_guard<net::io_context::executor_type> worker = net::make_work_guard(ioc);

    // the socket is closed.
    Clock::time_point t1 = Clock::now();

    auto session = std::make_shared<WebsocketSession>(ioc);
    bool canQuit = false;
    Stage stage = kStageInit;

    int messageNo = 0;

    while (!canQuit)
    {
        ioc.poll();
        do
        {
            Clock::time_point t2 = Clock::now();
            switch(stage)
            {
                case kStageInit:
                {
                    session->run(host, port);
                    stage = kStageConnecting;
                    break;
                }
                case kStageConnecting:
                {
                    if(session->isConnected())
                    {
                        stage = kStageConnected;
                    }
                    break;
                }
                case kStageConnected:
                {
                    //conntected. send message
                    if(session->getRecvCount() <= 20)
                    {
                        /**
                         * when recv 11 messages, server will sleep,
                         * but this client program will continue to send.
                         * So idle timeout should trigger.
                         */
                        int sendCntTogether = rand() % 5;
                        for(int i = 0; i <= sendCntTogether; ++i)
                        {
                            ++messageNo;
                            std::stringstream msg;
                            msg << "userMsg:" << messageNo;
                            session->sendMessage(msg.str());
                        }

                    }
                    else
                    {
                        session->close();
                        stage = kStageClosing;
                        worker.reset();
                    }

                    break;
                }
                default:
                {
                    //closing
                    if(ioc.stopped())
                    {
                        canQuit = true;
                    }
                    break;
                }
            }

            usleep(10 * 1000);

        }while(false);

    }


    return EXIT_SUCCESS;
}
