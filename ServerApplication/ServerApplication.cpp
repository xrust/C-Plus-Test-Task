#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <iostream>
#include <fstream>
#include <set>
#include <thread>
#include <mutex>
#include <cmath>
#include <chrono>
#include <atomic>
#include <vector>
#include <filesystem>
#include <conio.h>

using boost::asio::ip::tcp;
namespace asio = boost::asio;

class Server{
    public:
    Server( asio::io_context& io_context, unsigned short port, int dump_interval_seconds )
        : io_context_( io_context ),
        acceptor_( io_context, tcp::endpoint( tcp::v4(), port ) ),
        dump_interval_seconds_( dump_interval_seconds ),
        stop_flag_( false ),
        log_file_( "server_log.txt", std::ios::app ){
        std::cout << "Server started on port " << port << std::endl;
        log( "Server started on port " + std::to_string( port ) );

        // Start the dump thread
        dump_thread_ = std::thread( &Server::dump_thread_func, this );

        start_accept();
    }

    ~Server(){
        stop();
    }

    void stop(){
        if( !stop_flag_.exchange( true ) ){
            std::cout << "Stopping server..." << std::endl;
            log( "Stopping server..." );

            // Close acceptor
            boost::system::error_code ec;
            acceptor_.close( ec );

            // Join the dump thread
            if( dump_thread_.joinable() ){
                dump_thread_.join();
            }

            log( "Server stopped" );
            std::cout << "Server stopped" << std::endl;
        }
    }

    private:
    void start_accept(){
        if( stop_flag_ ) return;

        acceptor_.async_accept(
            [ this ]( const boost::system::error_code& error, tcp::socket socket ){
            if( !error ){
                std::cout << "New client connected: " << socket.remote_endpoint() << std::endl;
                log( "New client connected: " + socket.remote_endpoint().address().to_string() + ":" +
                     std::to_string( socket.remote_endpoint().port() ) );

                std::make_shared<ClientSession>( std::move( socket ), this )->start();
            }

            // Continue accepting new connections
            start_accept();
        } );
    }

    class ClientSession: public std::enable_shared_from_this<ClientSession>{
        public:
        ClientSession( tcp::socket socket, Server* server )
            : socket_( std::move( socket ) ), server_( server ){}

        void start(){
            read_number();
        }

        private:
        void read_number(){
            auto self( shared_from_this() );
            asio::async_read( socket_, asio::buffer( &number_, sizeof( number_ ) ),
                              [ this, self ]( boost::system::error_code ec, std::size_t length ){
                if( !ec ){
                    // Process received number
                    int received_num = ntohl( number_ );

                    if( received_num >= 0 && received_num <= 1023 ){
                        std::cout << "Received number: " << received_num << std::endl;
                        server_->log( "Received number: " + std::to_string( received_num ) );

                        double mean = server_->add_number( received_num );

                        // Convert the result to network byte order and send back
                        float result = static_cast< float >( mean );
                        int32_t* result_bits = reinterpret_cast< int32_t* >( &result );
                        int32_t network_result = htonl( *result_bits );

                        asio::async_write( socket_, asio::buffer( &network_result, sizeof( network_result ) ),
                                           [ this, self, mean ]( boost::system::error_code ec, std::size_t length ){
                            if( !ec ){
                                server_->log( "Sent result: " + std::to_string( mean ) );
                                read_number(); // Continue reading
                            } else{
                                handle_error( ec );
                            }
                        } );
                    } else{
                        std::cout << "Received invalid number: " << received_num << std::endl;
                        server_->log( "Received invalid number: " + std::to_string( received_num ) );
                        read_number(); // Continue reading
                    }
                } else{
                    handle_error( ec );
                }
            } );
        }

        void handle_error( const boost::system::error_code& ec ){
            try{
                if( ec == asio::error::eof ){
                    std::cout << "Client disconnected: " << socket_.remote_endpoint() << std::endl;
                    server_->log( "Client disconnected: " + socket_.remote_endpoint().address().to_string() + ":" +
                                  std::to_string( socket_.remote_endpoint().port() ) );
                } else{
                    std::cout << "Error: " << ec.message() << std::endl;
                    server_->log( "Error: " + ec.message() );
                }
            } catch( const std::exception& e ){
                std::cout << "Error while handling error: " << e.what() << std::endl;
                server_->log( "Error while handling error: " + std::string( e.what() ) );
            }
        }

        tcp::socket socket_;
        Server* server_;
        int32_t number_;
    };

    double add_number( int number ){
        std::lock_guard<std::mutex> lock( mutex_ );

        // Add number to container if unique
        numbers_.insert( number );

        // Calculate mean of squares
        double sum_of_squares = 0;
        for( int num : numbers_ ){
            sum_of_squares += static_cast< double >( num * num );
        }

        return numbers_.empty() ? 0 : sum_of_squares / numbers_.size();
    }

    void dump_thread_func(){
        while( !stop_flag_ ){
            // Sleep for the specified interval
            for( int i = 0; i < dump_interval_seconds_ && !stop_flag_; ++i ){
                std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
            }

            if( stop_flag_ ) break;

            create_dump();
        }
    }

    void create_dump(){
        std::string filename = "server_dump_" + get_timestamp() + ".bin";
        std::cout << "Creating dump: " << filename << std::endl;
        log( "Creating dump: " + filename );

        std::vector<int> numbers_copy;
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            numbers_copy.assign( numbers_.begin(), numbers_.end() );
        }

        std::ofstream dump_file( filename, std::ios::binary );
        if( dump_file ){
            int32_t count = static_cast< int32_t >( numbers_copy.size() );
            dump_file.write( reinterpret_cast< char* >( &count ), sizeof( count ) );

            for( int num : numbers_copy ){
                int32_t network_num = htonl( num );
                dump_file.write( reinterpret_cast< char* >( &network_num ), sizeof( network_num ) );
            }

            log( "Dump created successfully with " + std::to_string( numbers_copy.size() ) + " numbers" );
        } else{
            std::cerr << "Failed to create dump file" << std::endl;
            log( "Failed to create dump file" );
        }
    }

    std::string get_timestamp(){
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t( now );

        std::tm tm_now;
        localtime_s( &tm_now, &time_t_now );

        char buffer[ 32 ];
        std::strftime( buffer, sizeof( buffer ), "%Y%m%d_%H%M%S", &tm_now );

        return std::string( buffer );
    }

    void log( const std::string& message ){
        std::string timestamped_message = get_timestamp() + " - " + message;

        // Lock for file access
        std::lock_guard<std::mutex> lock( log_mutex_ );

        if( log_file_ ){
            log_file_ << timestamped_message << std::endl;
            log_file_.flush();
        }
    }

    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    int dump_interval_seconds_;
    std::atomic<bool> stop_flag_;
    std::thread dump_thread_;
    std::set<int> numbers_;
    std::mutex mutex_;
    std::ofstream log_file_;
    std::mutex log_mutex_;
};

int main(){
    try{
        asio::io_context io_context;

        // Handle Ctrl+C and other signals
        asio::signal_set signals( io_context, SIGINT, SIGTERM );

        // Create the server with 30-second dump interval
        Server server( io_context, 8080, 30 );

        signals.async_wait( [ & ]( const boost::system::error_code& error, int signal_number ){
            std::cout << "Signal received, stopping server..." << std::endl;
            server.stop();
            io_context.stop();
        } );

        std::cout << "Server running. Press ESC to stop." << std::endl;

        // Thread for handling ESC key
        std::thread key_thread( [ & ](){
            while( true ){
                if( _kbhit() && _getch() == 27 ){ // ESC key
                    std::cout << "ESC pressed, stopping server..." << std::endl;
                    server.stop();
                    io_context.stop();
                    break;
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            }
        } );

        // Run the IO context with an executor to prevent it from stopping when there are no more tasks
        asio::executor_work_guard<asio::io_context::executor_type> work_guard =
            asio::make_work_guard( io_context );

        // Run the IO context
        io_context.run();

        if( key_thread.joinable() ){
            key_thread.join();
        }
    } catch( std::exception& e ){
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
