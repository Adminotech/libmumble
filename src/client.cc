#include "client.h"

#include <boost/make_shared.hpp>
#include <deque>
#include <typeinfo>
#include <iostream>

#include "channel.h"
#include "CryptState.h"
#include "logging.h"
#include "settings.h"
#include "user.h"

#ifndef SAFE_DELETE
#define SAFE_DELETE(p) { delete p; p=0; }
#endif

///////////////////////////////////////////////////////////////////////////////

namespace 
{
    template <class T>
    T ConstructProtobufObject(void* buffer, int32_t length, bool print) 
    {
        T pb;
        pb.ParseFromArray(buffer, length);
        if (print) {
            DLOG(INFO) << ">> IN: " << typeid(T).name() << ":";
            DLOG(INFO) << pb.DebugString();
        }
        return pb;
    }

    inline int32_t MUMBLE_VERSION(int16_t x, int16_t y, int16_t z) 
    {
        return (x << 16) | (y << 8) | (z & 0xFF);
    }
}

///////////////////////////////////////////////////////////////////////////////

namespace MumbleClient 
{
    class MessageHeader 
    {
    public:
        int16_t type() const { return (d_[0] << 8) | d_[1]; }
        int32_t length() const { return (d_[2] << 24) | (d_[3] << 16) | (d_[4] << 8) | d_[5]; }

        void type(int16_t t_) { d_[0] = t_ >> 8; d_[1] = t_ & 0xFF; }
        void length(int32_t l_) 
        {
            d_[2] = static_cast<unsigned char>(l_ >> 24);
            d_[3] = static_cast<unsigned char>(l_ >> 16);
            d_[4] = static_cast<unsigned char>(l_ >> 8);
            d_[5] = static_cast<unsigned char>(l_ & 0xFF);
        }

        const unsigned char* data() const { return d_; }

        friend std::istream& operator>>(std::istream& is, MessageHeader& header) 
        {
            return is.read(reinterpret_cast<char*>(header.d_), 6);
        }

    private:
        unsigned char d_[6];
    };

    class Message 
    {
    public:
        Message(const MessageHeader& header, const std::string& msg) : header_(header), msg_(msg) { };

        MessageHeader header_;
        std::string msg_;
    };

    ///////////////////////////////////////////////////////////////////////////////

    MumbleClient::MumbleClient(boost::asio::io_service* io_service) :
        io_service_(io_service),
        cs_(new CryptState()),
        state_(kStateNew),
        processing_tcp_queue_(false),
        resolving_(false),
        ping_timer_(0),
        tcp_socket_(0),
        udp_socket_(0),
        resolver_(0)
    {
        currentSettings_ = Settings();
        resolver_ = new boost::asio::ip::tcp::resolver(*io_service_);
    }

    MumbleClient::~MumbleClient() 
    {
        if (state_ != kStateDisconnected)
            Disconnect();

        try
        {
            if (ping_timer_)
            {
                //LOG(INFO) << "-- Deleting ping timer";
                SAFE_DELETE(ping_timer_);
            }
            if (tcp_socket_)
            {
                //LOG(INFO) << "-- Deleting TCP socket";
                SAFE_DELETE(tcp_socket_);
            }
            if (udp_socket_)
            {
                //LOG(INFO) << "-- Deleting UDP socket";
                SAFE_DELETE(udp_socket_);
            }
            if (resolver_)
            {
                //LOG(INFO) << "-- Deleting host resolver";
                SAFE_DELETE(resolver_);
            }
            if (cs_)
            {
                //LOG(INFO) << "-- Deleting crypt state";
                SAFE_DELETE(cs_);
            }
        }
        catch(std::exception &e)
        {
            std::cout << "Error in ~MumbleClient(): " << e.what() << std::endl;
        }
    }

    void MumbleClient::Connect(const Settings& s) 
    {
        if (!resolver_)
        {
            LOG(ERROR) << "libmumble: My host resolver is null, cannot proceed!";
            return;
        }
        if (resolving_)
        {
            LOG(INFO) << "Already connecting, please wait...";
            return;
        }

        // Resolve host
        LOG(INFO) << "libmumble: Resolving host " << s.GetHost() << ":" << s.GetPort();

        state_ = kStateNew;
        currentSettings_ = Settings(s.GetHost(), s.GetPort(), s.GetUserName(), s.GetPassword());

        // Note: 'io_service_' needs to be running so it will process the queued async_resolve() call!
        resolving_ = true;
        boost::asio::ip::tcp::resolver::query query(currentSettings_.GetHost(), currentSettings_.GetPort());
        resolver_->async_resolve(query, boost::bind(&MumbleClient::OnConnected, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
    }

    void MumbleClient::OnConnected(const boost::system::error_code& resolveError, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
    {
        resolving_ = false;
        boost::asio::ip::tcp::resolver::iterator end;

        // Prepare connection
#if SSL
        boost::asio::ssl::context ctx(*io_service_, boost::asio::ssl::context::tlsv1);
        tcp_socket_ = new boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(*io_service_, ctx);
#else
        tcp_socket_ = new boost::asio::ip::tcp::socket(*io_service_);
#endif

        // Try to connect TCP
        boost::system::error_code error = boost::asio::error::host_not_found;
        while (error && endpoint_iterator != end) 
        {
            LOG(INFO) << "libmumble: Connecting to " << (*endpoint_iterator).endpoint().address();
#if SSL
            tcp_socket_->lowest_layer().close();
            tcp_socket_->lowest_layer().connect(*endpoint_iterator++, error);
#else
            tcp_socket_->close();
            tcp_socket_->connect(*endpoint_iterator++, error);
#endif
        }

        // Handle errors
        if (error)
        {
            if (error_callback_)
                error_callback_(error);
            else
                LOG(ERROR) << "libmumble: Connection error: " << error.message();

            if (connected_callback_)
                connected_callback_(false, currentSettings_, error.message());
            else
                LOG(ERROR) << "libmumble: Connected successfully but not callback has been set, use SetConnectedCallback() to set one!";
            return;
        }

#if SSL
        // Do SSL handshake
        boost::asio::ip::udp::endpoint udp_endpoint(tcp_socket_->lowest_layer().remote_endpoint().address(), tcp_socket_->lowest_layer().remote_endpoint().port());
        udp_socket_ = new boost::asio::ip::udp::socket(*io_service_);
        udp_socket_->connect(udp_endpoint, error);

        tcp_socket_->handshake(boost::asio::ssl::stream_base::client, error);
        if (error) 
        {
            if (error_callback_)
                error_callback_(error);
            else
                LOG(ERROR) << "libmumble: handshake error: " << error.message();

            if (connected_callback_)
                connected_callback_(false, currentSettings_, error.message());
            else
                LOG(ERROR) << "libmumble: Connected successfully but not callback has been set, use SetConnectedCallback() to set one!";
            return;
        }
#endif

        state_ = kStateHandshakeCompleted;

        // Setup connection params
        boost::asio::socket_base::non_blocking_io nbio_command(true);
        boost::asio::ip::tcp::no_delay no_delay_option(true);

#if SSL
        tcp_socket_->lowest_layer().io_control(nbio_command);
        tcp_socket_->lowest_layer().set_option(no_delay_option);
#else
        tcp_socket_->io_control(nbio_command);
        tcp_socket_->set_option(no_delay_option);
#endif

        // Send initial messages
        MumbleProto::Version v;
        v.set_version(MUMBLE_VERSION(1, 2, 2));
        v.set_release("libmumbleclient-0.0.2");
        SendMessage(PbMessageType::Version, v, true);

        MumbleProto::Authenticate a;
        a.set_username(currentSettings_.GetUserName());
        a.set_password(currentSettings_.GetPassword());
        a.add_celt_versions(0x8000000b); // FIXME(pcgod): hardcoded version number
        SendMessage(PbMessageType::Authenticate, a, true);

        boost::asio::async_read(*tcp_socket_, recv_buffer_, boost::asio::transfer_at_least(6), boost::bind(&MumbleClient::ReadHandler, this, boost::asio::placeholders::error));

        if (connected_callback_)
            connected_callback_(true, currentSettings_, "");
        else
            LOG(ERROR) << "libmumble: Connected successfully but not callback has been set, use SetConnectedCallback() to set one!";
    }

    void MumbleClient::Disconnect() 
    {
        state_ = kStateDisconnected;

        std::cout << "libmumble: Disconnecting" << std::endl;

        if (ping_timer_)
        {    
            std::cout << "-- Canceling ping" << std::endl;
            try { ping_timer_->cancel(); }
            catch(boost::system::system_error &error) { std::cout << "   Error: ping_timer_->cancel() : " << error.what() << std::endl; }
            // Don't wait on the last ping, lets assume killing the timer is ok
            // even if a pending async ping is coming to SendPing().
            // The wait would block 0-5000 msec depending how far its ticking atm.
            //std::cout << "-- Waiting last ping" << std::endl;
            //try { ping_timer_->wait(); }
            //catch(boost::system::system_error &error) { std::cout << "   Error: ping_timer_->wait()   : " << error.what() << std::endl; }
            SAFE_DELETE(ping_timer_);
        }

        std::cout << "-- Clearing user/channel lists" << std::endl;
        send_queue_.clear();
        user_list_.clear();
        channel_list_.clear();

        if (udp_socket_)
        {
            std::cout << "-- Closing UDP socket" << std::endl;
            try { udp_socket_->cancel(); }
            catch(boost::system::system_error &error) { std::cout << "   Error: udp_socket_->cancel() : " << error.what() << std::endl; }
            try { udp_socket_->close(); }
            catch(boost::system::system_error &error) { std::cout << "   Error: udp_socket_->close()  : " << error.what() << std::endl; }
            SAFE_DELETE(udp_socket_);
        }

        if (tcp_socket_)
        {
            std::cout << "-- Closing TCP socket" << std::endl;
            try { tcp_socket_->lowest_layer().cancel(); }
            catch(boost::system::system_error &error) { std::cout << "   Error: tcp_socket_->cancel() : " << error.what() << std::endl; }
            try { tcp_socket_->lowest_layer().close(); }
            catch(boost::system::system_error &error) { std::cout << "   Error: tcp_socket_->close()  : " << error.what() << std::endl; }
            SAFE_DELETE(tcp_socket_);
        }
    }

    void MumbleClient::SendPing(const boost::system::error_code& error) 
    {
        if (state_ == kStateDisconnected)
            return;

        if (error) 
        {
            if (error_callback_)
                error_callback_(error);
            else
                LOG(ERROR) << "libmumble::SendPing: " << error.message();
            return;
        }

        MumbleProto::Ping p;
        p.set_timestamp(std::time(NULL));
        SendMessage(PbMessageType::Ping, p, false);

        // Requeue ping
        if (!ping_timer_)
            ping_timer_ = new boost::asio::deadline_timer(*io_service_);

        ping_timer_->expires_from_now(boost::posix_time::seconds(5));
        ping_timer_->async_wait(boost::bind(&MumbleClient::SendPing, this, boost::asio::placeholders::error));
    }

    void MumbleClient::ParseMessage(const MessageHeader& msg_header, void* buffer) 
    {
        switch (msg_header.type()) 
        {
        case PbMessageType::Version: 
        {
            MumbleProto::Version v = ConstructProtobufObject<MumbleProto::Version>(buffer, msg_header.length(), true);
            // NOT_IMPLEMENTED
            LOG(INFO) << "libmumble: PbMessageType::Version handling not implemented!";
            break;
        }
        case PbMessageType::Ping:
        {
            MumbleProto::Ping p = ConstructProtobufObject<MumbleProto::Ping>(buffer, msg_header.length(), false);
            // NOT_IMPLEMENTED
            //LOG(INFO) << "PbMessageType::Ping handling not implemented!";
            break;
        }
        case PbMessageType::ChannelRemove: 
        {
            MumbleProto::ChannelRemove cr = ConstructProtobufObject<MumbleProto::ChannelRemove>(buffer, msg_header.length(), true);
            HandleChannelRemove(cr);
            break;
        }
        case PbMessageType::ChannelState: 
        {
            MumbleProto::ChannelState cs = ConstructProtobufObject<MumbleProto::ChannelState>(buffer, msg_header.length(), true);
            HandleChannelState(cs);
            break;
        }
        case PbMessageType::UserRemove: 
        {
            MumbleProto::UserRemove ur = ConstructProtobufObject<MumbleProto::UserRemove>(buffer, msg_header.length(), true);
            HandleUserRemove(ur);
            break;
        }
        case PbMessageType::UserState: 
        {
            MumbleProto::UserState us = ConstructProtobufObject<MumbleProto::UserState>(buffer, msg_header.length(), true);
            HandleUserState(us);
            break;
        }
        case PbMessageType::TextMessage: 
        {
            MumbleProto::TextMessage tm = ConstructProtobufObject<MumbleProto::TextMessage>(buffer, msg_header.length(), true);
            if (text_message_callback_)
                text_message_callback_(tm.message());
            break;
        }
        case PbMessageType::CryptSetup: 
        {
            MumbleProto::CryptSetup cs = ConstructProtobufObject<MumbleProto::CryptSetup>(buffer, msg_header.length(), true);
            if (cs.has_key() && cs.has_client_nonce() && cs.has_server_nonce()) {
                cs_->setKey(reinterpret_cast<const unsigned char *>(cs.key().data()), reinterpret_cast<const unsigned char *>(cs.client_nonce().data()), reinterpret_cast<const unsigned char *>(cs.server_nonce().data()));
            } else if (cs.has_server_nonce()) {
                LOG(WARNING) << "Crypt resync";
                cs_->setDecryptIV(reinterpret_cast<const unsigned char *>(cs.server_nonce().data()));
            } else {
                cs.Clear();
                cs.set_client_nonce(reinterpret_cast<const char *>(cs_->getEncryptIV()));
                SendMessage(PbMessageType::CryptSetup, cs, true);
            }
            break;
        }
        case PbMessageType::CodecVersion: 
        {
            MumbleProto::CodecVersion cv = ConstructProtobufObject<MumbleProto::CodecVersion>(buffer, msg_header.length(), true);
            // NOT_IMPLEMENTED
            LOG(INFO) << "PbMessageType::CodecVersion handling not implemented!";
            break;
        }
        case PbMessageType::ServerSync: 
        {
            MumbleProto::ServerSync ss = ConstructProtobufObject<MumbleProto::ServerSync>(buffer, msg_header.length(), true);
            state_ = kStateAuthenticated;
            session_ = ss.session();

            // Enqueue ping
            SendPing(boost::system::error_code());

            if (auth_callback_)
                auth_callback_();
            break;
        }
        case PbMessageType::UDPTunnel: 
        {
            if (raw_udp_tunnel_callback_)
                raw_udp_tunnel_callback_(msg_header.length(), buffer);
            break;
        }
        default:
            DLOG(WARNING) << ">> IN: Unhandled message - Type: " << msg_header.type() << " Length: " << msg_header.length();
        }
    }

    boost::shared_ptr<User> MumbleClient::FindUser(int32_t session) {
        for (user_list_iterator it = user_list_.begin(); it != user_list_.end(); ++it) {
            if ((*it)->session == session)
                return *it;
        }

        return boost::shared_ptr<User>();
    }

    boost::shared_ptr<Channel> MumbleClient::FindChannel(int32_t id) {
        for (channel_list_iterator it = channel_list_.begin(); it != channel_list_.end(); ++it) {
            if ((*it)->id == id)
                return *it;
        }

        return boost::shared_ptr<Channel>();
    }

    void MumbleClient::HandleUserRemove(const MumbleProto::UserRemove& ur) {
        boost::shared_ptr<User> u = FindUser(ur.session());
        assert(u);

        if (u) {
            user_list_.remove(u);

            if (user_left_callback_)
                user_left_callback_(*u);
        }
    }

    void MumbleClient::HandleUserState(const MumbleProto::UserState& us) {
        boost::shared_ptr<User> u = FindUser(us.session());
        if (!u) {
            // New user
            boost::shared_ptr<Channel> c = FindChannel(us.channel_id());
            assert(c);

            boost::shared_ptr<User> nu = boost::make_shared<User>(us.session(), c);
            nu->name = us.name();
            if (us.has_hash())
                nu->hash = us.hash();

            if (us.has_comment())
                nu->comment = us.comment();

            DLOG(INFO) << "New user " << nu->name;
            user_list_.push_back(nu);

            if (user_joined_callback_)
                user_joined_callback_(*nu);

            return;
        }

        DLOG(INFO) << "Found user " << u->name;
        if (us.has_channel_id()) {
            // Channel changed
            boost::shared_ptr<Channel> c = FindChannel(us.channel_id());
            assert(c);

            boost::shared_ptr<Channel> oc = u->channel.lock();
            u->channel = c;

            if (user_moved_callback_)
                user_moved_callback_(*u, *oc);
        }

        if (us.has_comment()) {
            u->comment = us.comment();
            // user_comment_changed_callback_
        }
    }

    void MumbleClient::HandleChannelRemove(const MumbleProto::ChannelRemove& cr) {
        boost::shared_ptr<Channel> c = FindChannel(cr.channel_id());
        assert(c);

        if (c) {
            channel_list_.remove(c);

            if (channel_remove_callback_)
                channel_remove_callback_(*c);
        }
    }

    void MumbleClient::HandleChannelState(const MumbleProto::ChannelState& cs) {
        boost::shared_ptr<Channel> c = FindChannel(cs.channel_id());
        if (!c) {
            // New channel
            boost::shared_ptr<Channel> nc = boost::make_shared<Channel>(cs.channel_id());
            nc->name = cs.name();

            if (cs.parent() != 0) {
                boost::shared_ptr<Channel> p = FindChannel(cs.parent());
                assert(p);
                nc->parent = p;
            }

            DLOG(INFO) << "New channel " << nc->name;
            channel_list_.push_back(nc);

            if (channel_add_callback_)
                channel_add_callback_(*nc);

            return;
        }

        DLOG(INFO) << "Found channel " << c->name;
    }

    #if !defined(NDEBUG)
    void MumbleClient::PrintChannelList() {
        DLOG(INFO) << "-- Channel list --";
        for (channel_list_iterator it = channel_list_.begin(); it != channel_list_.end(); ++it) {
            DLOG(INFO) << "Channel " << (*it)->name;
        }
        DLOG(INFO) << "-- Channel list end --";
    }

    void MumbleClient::PrintUserList() {
        DLOG(INFO) << "-- User list --";
        for (user_list_iterator it = user_list_.begin(); it != user_list_.end(); ++it) {
            DLOG(INFO) << "User " << (*it)->name << " on " << (*it)->channel.lock()->name;
        }
        DLOG(INFO) << "-- User list end --";
    }
    #endif


    void MumbleClient::ProcessTCPSendQueue(const boost::system::error_code& error, const size_t /*bytes_transferred*/) 
    {
        if (state_ == kStateDisconnected)
            return;

        if (!error) 
        {
            send_queue_.pop_front();
            if (send_queue_.empty())
            {
                processing_tcp_queue_ = false;
                return;
            }

            SendFirstQueued();
        } 
        else 
        {
            processing_tcp_queue_ = false;
            if (error_callback_)
                error_callback_(error);
            else
                LOG(ERROR) << "Write error: " << error.message();
        }
    }

    void MumbleClient::SendFirstQueued() 
    {
        processing_tcp_queue_ = true;
        boost::shared_ptr<Message>& msg = send_queue_.front();

        std::vector<boost::asio::const_buffer> bufs;
        bufs.push_back(boost::asio::buffer(msg->header_.data(), 6));
        bufs.push_back(boost::asio::buffer(msg->msg_, msg->msg_.size()));

        async_write(*tcp_socket_, bufs, boost::bind(&MumbleClient::ProcessTCPSendQueue, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        DLOG(INFO) << "<< ASYNC Type: " << msg->header_.type() << " Length: 6+" << msg->msg_.size();
    }

    bool MumbleClient::HandleMessageContent(std::istream& is, const MessageHeader& msg_header) {
        if (static_cast<int32_t>(recv_buffer_.size()) < msg_header.length()) {
            // The message is incomplete, read the rest
            if (tcp_socket_)
                async_read(*tcp_socket_, recv_buffer_, boost::asio::transfer_at_least(msg_header.length() - recv_buffer_.size()), boost::bind(&MumbleClient::ReadHandlerContinue, this, msg_header, boost::asio::placeholders::error));
            return false;
        }

        // Receive message body
        char* buffer = new char[msg_header.length()];
        is.read(buffer, msg_header.length());
        ParseMessage(msg_header, buffer);
        delete[] buffer;

        return true;
    }

    void MumbleClient::ReadHandler(const boost::system::error_code& error) 
    {
        if (state_ == kStateDisconnected)
            return;

        if (error) 
        {
            if (error_callback_)
                error_callback_(error);
            else
                LOG(ERROR) << "read error: " << error.message();
            return;
        }

        std::istream is(&recv_buffer_);
        do {
            // Receive message header
            MessageHeader msg_header;
            is >> msg_header;

            if (msg_header.length() >= 0x7FFFF)
                assert(false);

            if (!HandleMessageContent(is, msg_header))
                return;
        } while (recv_buffer_.size() >= 6);

        // Requeue read
        if (tcp_socket_)
            async_read(*tcp_socket_, recv_buffer_, boost::asio::transfer_at_least(6), boost::bind(&MumbleClient::ReadHandler, this, boost::asio::placeholders::error));
    }

    void MumbleClient::ReadHandlerContinue(const MessageHeader msg_header, const boost::system::error_code& error) 
    {
        if (state_ == kStateDisconnected)
            return;

        if (error) 
        {    
            if (error_callback_)
                error_callback_(error);
            else
                LOG(ERROR) << "read error: " << error.message();
            return;
        }

        std::istream is(&recv_buffer_);
        HandleMessageContent(is, msg_header);

        // Requeue read
        if (tcp_socket_)
            async_read(*tcp_socket_, recv_buffer_, boost::asio::transfer_at_least(6), boost::bind(&MumbleClient::ReadHandler, this, boost::asio::placeholders::error));
    }

    void MumbleClient::SendMessage(PbMessageType::MessageType type, const ::google::protobuf::Message& new_msg, bool print) {
        if (print) {
            DLOG(INFO) << "<< ENQUEUE: " << type;
            DLOG(INFO) << new_msg.DebugString();
        }

        int32_t length = new_msg.ByteSize();
        MessageHeader msg_header;
        msg_header.type(static_cast<int16_t>(type));
        msg_header.length(length);

        std::string pb_message = new_msg.SerializeAsString();
        boost::shared_ptr<Message> m = boost::make_shared<Message>(msg_header, pb_message);
        send_queue_.push_back(m);

        if (state_ >= kStateHandshakeCompleted && !processing_tcp_queue_) {
            SendFirstQueued();
        }
    }

    void MumbleClient::SendRawUdpTunnel(const char* buffer, int32_t len) {
        MessageHeader msg_header;
        msg_header.type(PbMessageType::UDPTunnel);
        msg_header.length(len);

        std::string data(buffer, len);
        boost::shared_ptr<Message> m = boost::make_shared<Message>(msg_header, data);
        send_queue_.push_back(m);

        if (state_ >= kStateHandshakeCompleted && !processing_tcp_queue_) {
            SendFirstQueued();
        }
    }

    void MumbleClient::SendUdpMessage(const char* buffer, int32_t len) {
        assert(cs_->isValid());

        unsigned char* buf = new unsigned char[len + 4];
        cs_->encrypt(reinterpret_cast<const unsigned char *>(buffer), buf, len);
        udp_socket_->send(boost::asio::buffer(buf, len + 4));

        delete[] buf;
    }

    void MumbleClient::SetComment(const std::string& text) {
        BOOST_ASSERT(state_ >= kStateAuthenticated);

        MumbleProto::UserState us;
        us.set_session(session_);
        us.set_comment(text);

        SendMessage(PbMessageType::UserState, us, true);
    }

    void MumbleClient::JoinChannel(int32_t channel_id) {
        BOOST_ASSERT(state_ >= kStateAuthenticated);

        MumbleProto::UserState us;
        us.set_session(session_);
        us.set_channel_id(channel_id);

        SendMessage(PbMessageType::UserState, us, true);
    }

}  // namespace MumbleClient
