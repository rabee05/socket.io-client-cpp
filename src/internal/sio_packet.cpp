//
//  sio_packet.cpp
//
//  Created by Melo Yao on 3/22/15.
//  Migrated to simdjson for better performance
//

#include "sio_packet.h"
#include <simdjson.h>
#include <cassert>
#include <algorithm>
#include <sstream>

#define kBIN_PLACE_HOLDER "_placeholder"

namespace sio
{
    using namespace std;

    // Forward declarations
    void accept_message(message const& msg, string& json_str, vector<shared_ptr<const string> >& buffers, bool is_first = true);
    string escape_json_string(const string& input);

    // Helper: Escape JSON string
    string escape_json_string(const string& input)
    {
        ostringstream output;
        for (char c : input) {
            switch (c) {
                case '"':  output << "\\\""; break;
                case '\\': output << "\\\\"; break;
                case '\b': output << "\\b";  break;
                case '\f': output << "\\f";  break;
                case '\n': output << "\\n";  break;
                case '\r': output << "\\r";  break;
                case '\t': output << "\\t";  break;
                default:
                    if (c >= 0 && c < 32) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        output << buf;
                    } else {
                        output << c;
                    }
            }
        }
        return output.str();
    }

    void accept_bool_message(bool_message const& msg, string& json_str)
    {
        json_str += msg.get_bool() ? "true" : "false";
    }

    void accept_null_message(string& json_str)
    {
        json_str += "null";
    }

    void accept_int_message(int_message const& msg, string& json_str)
    {
        json_str += to_string(msg.get_int());
    }

    void accept_double_message(double_message const& msg, string& json_str)
    {
        ostringstream ss;
        ss.precision(15);
        ss << msg.get_double();
        json_str += ss.str();
    }

    void accept_string_message(string_message const& msg, string& json_str)
    {
        json_str += "\"";
        json_str += escape_json_string(msg.get_string());
        json_str += "\"";
    }

    void accept_binary_message(binary_message const& msg, string& json_str, vector<shared_ptr<const string> >& buffers)
    {
        json_str += "{\"" kBIN_PLACE_HOLDER "\":true,\"num\":";
        json_str += to_string(buffers.size());
        json_str += "}";
        buffers.push_back(msg.get_binary());
    }

    void accept_array_message(array_message const& msg, string& json_str, vector<shared_ptr<const string> >& buffers)
    {
        json_str += "[";
        bool first = true;
        for (vector<message::ptr>::const_iterator it = msg.get_vector().begin(); it != msg.get_vector().end(); ++it) {
            if (!first) json_str += ",";
            first = false;
            accept_message(*(*it), json_str, buffers, false);
        }
        json_str += "]";
    }

    void accept_object_message(object_message const& msg, string& json_str, vector<shared_ptr<const string> >& buffers)
    {
        json_str += "{";
        bool first = true;
        for (map<string, message::ptr>::const_iterator it = msg.get_map().begin(); it != msg.get_map().end(); ++it) {
            if (!first) json_str += ",";
            first = false;
            json_str += "\"";
            json_str += escape_json_string(it->first);
            json_str += "\":";
            accept_message(*(it->second), json_str, buffers, false);
        }
        json_str += "}";
    }

    void accept_message(message const& msg, string& json_str, vector<shared_ptr<const string> >& buffers, bool is_first)
    {
        const message* msg_ptr = &msg;
        switch (msg.get_flag())
        {
        case message::flag_integer:
            accept_int_message(*(static_cast<const int_message*>(msg_ptr)), json_str);
            break;
        case message::flag_double:
            accept_double_message(*(static_cast<const double_message*>(msg_ptr)), json_str);
            break;
        case message::flag_string:
            accept_string_message(*(static_cast<const string_message*>(msg_ptr)), json_str);
            break;
        case message::flag_boolean:
            accept_bool_message(*(static_cast<const bool_message*>(msg_ptr)), json_str);
            break;
        case message::flag_null:
            accept_null_message(json_str);
            break;
        case message::flag_binary:
            accept_binary_message(*(static_cast<const binary_message*>(msg_ptr)), json_str, buffers);
            break;
        case message::flag_array:
            accept_array_message(*(static_cast<const array_message*>(msg_ptr)), json_str, buffers);
            break;
        case message::flag_object:
            accept_object_message(*(static_cast<const object_message*>(msg_ptr)), json_str, buffers);
            break;
        default:
            break;
        }
    }

    // Parse JSON using simdjson DOM API
    message::ptr from_json(simdjson::dom::element const& value, vector<shared_ptr<const string> > const& buffers)
    {
        try {
            if (value.is_int64()) {
                return int_message::create(int64_t(value));
            }
            else if (value.is_uint64()) {
                return int_message::create(int64_t(uint64_t(value)));
            }
            else if (value.is_double()) {
                return double_message::create(double(value));
            }
            else if (value.is_string()) {
                string_view sv = value.get_string();
                string str(sv.data(), sv.length());
                return string_message::create(str);
            }
            else if (value.is_array()) {
                message::ptr ptr = array_message::create();
                auto arr = value.get_array();
                for (auto child : arr) {
                    static_cast<array_message*>(ptr.get())->get_vector().push_back(from_json(child, buffers));
                }
                return ptr;
            }
            else if (value.is_object()) {
                auto obj = value.get_object();

                // Check for binary placeholder
                simdjson::dom::element placeholder_elem;
                auto placeholder_error = obj[kBIN_PLACE_HOLDER].get(placeholder_elem);

                if (placeholder_error == simdjson::SUCCESS && placeholder_elem.is_bool() && bool(placeholder_elem)) {
                    simdjson::dom::element num_elem;
                    auto num_error = obj["num"].get(num_elem);
                    if (num_error == simdjson::SUCCESS && num_elem.is_int64()) {
                        int num = int(int64_t(num_elem));
                        if (num >= 0 && num < static_cast<int>(buffers.size())) {
                            return binary_message::create(buffers[num]);
                        }
                    }
                    return message::ptr();
                }

                // Real object message
                message::ptr ptr = object_message::create();
                for (auto field : obj) {
                    string_view key_sv = field.key;
                    string key(key_sv.data(), key_sv.length());
                    static_cast<object_message*>(ptr.get())->get_map()[key] = from_json(field.value, buffers);
                }
                return ptr;
            }
            else if (value.is_bool()) {
                return bool_message::create(bool(value));
            }
            else if (value.is_null()) {
                return null_message::create();
            }
        }
        catch (...) {
            // If parsing fails, return null
        }
        return message::ptr();
    }

    packet::packet(string const& nsp, message::ptr const& msg, int pack_id, bool isAck) :
        _frame(frame_message),
        _type((isAck ? type_ack : type_event) | type_undetermined),
        _nsp(nsp),
        _pack_id(pack_id),
        _message(msg),
        _pending_buffers(0)
    {
        assert((!isAck || (isAck && pack_id >= 0)));
    }

    packet::packet(type type, string const& nsp, message::ptr const& msg) :
        _frame(frame_message),
        _type(type),
        _nsp(nsp),
        _pack_id(-1),
        _message(msg),
        _pending_buffers(0)
    {
    }

    packet::packet(packet::frame_type frame) :
        _frame(frame),
        _type(type_undetermined),
        _pack_id(-1),
        _pending_buffers(0)
    {
    }

    packet::packet() :
        _type(type_undetermined),
        _pack_id(-1),
        _pending_buffers(0)
    {
    }

    bool packet::is_binary_message(string const& payload_ptr)
    {
        return payload_ptr.size() > 0 && payload_ptr[0] == frame_message;
    }

    bool packet::is_text_message(string const& payload_ptr)
    {
        return payload_ptr.size() > 0 && payload_ptr[0] == (frame_message + '0');
    }

    bool packet::is_message(string const& payload_ptr)
    {
        return is_binary_message(payload_ptr) || is_text_message(payload_ptr);
    }

    bool packet::parse_buffer(const string& buf_payload)
    {
        if (_pending_buffers > 0) {
            assert(is_binary_message(buf_payload)); // this is ensured by outside.
            _buffers.push_back(std::make_shared<string>(buf_payload.data(), buf_payload.size()));
            _pending_buffers--;
            if (_pending_buffers == 0) {
                // Parse using simdjson
                simdjson::dom::parser parser;
                simdjson::dom::element doc;
                auto error = parser.parse(_buffers.front()->data(), _buffers.front()->size()).get(doc);
                _buffers.erase(_buffers.begin());

                if (error == simdjson::SUCCESS) {
                    _message = from_json(doc, _buffers);
                }
                _buffers.clear();
                return false;
            }
            return true;
        }
        return false;
    }

    bool packet::parse(const string& payload_ptr)
    {
        assert(!is_binary_message(payload_ptr)); // this is ensured by outside
        _frame = (packet::frame_type)(payload_ptr[0] - '0');
        _message.reset();
        _pack_id = -1;
        _buffers.clear();
        _pending_buffers = 0;
        size_t pos = 1;

        if (_frame == frame_message) {
            _type = (packet::type)(payload_ptr[pos] - '0');
            if (_type < type_min || _type > type_max) {
                return false;
            }
            pos++;
            if (_type == type_binary_event || _type == type_binary_ack) {
                size_t score_pos = payload_ptr.find('-');
                _pending_buffers = static_cast<unsigned>(std::stoul(payload_ptr.substr(pos, score_pos - pos)));
                pos = score_pos + 1;
            }
        }

        size_t nsp_json_pos = payload_ptr.find_first_of("{[\"/", pos, 4);
        if (nsp_json_pos == string::npos) { // no namespace and no message, the end.
            _nsp = "/";
            return false;
        }

        size_t json_pos = nsp_json_pos;
        if (payload_ptr[nsp_json_pos] == '/') { // nsp_json_pos is start of nsp
            size_t comma_pos = payload_ptr.find_first_of(","); // end of nsp
            if (comma_pos == string::npos) { // packet end with nsp
                _nsp = payload_ptr.substr(nsp_json_pos);
                return false;
            }
            else { // we have a message, maybe the message have an id.
                _nsp = payload_ptr.substr(nsp_json_pos, comma_pos - nsp_json_pos);
                pos = comma_pos + 1; // start of the message
                json_pos = payload_ptr.find_first_of("\"[{", pos, 3); // start of the json part of message
                if (json_pos == string::npos) {
                    // no message, the end
                    // assume if there's no message, there's no message id.
                    return false;
                }
            }
        }
        else {
            _nsp = "/";
        }

        if (pos < json_pos) { // we've got pack id.
            std::string pack_id_str = payload_ptr.substr(pos, json_pos - pos);

            if (std::all_of(pack_id_str.begin(), pack_id_str.end(), ::isdigit)) {
                _pack_id = std::stoi(pack_id_str);
            }
            else {
                _pack_id = -1;
            }
        }

        if (_frame == frame_message && (_type == type_binary_event || _type == type_binary_ack)) {
            // parse later when all buffers are arrived.
            _buffers.push_back(make_shared<string>(payload_ptr.data() + json_pos, payload_ptr.length() - json_pos));
            return true;
        }
        else {
            // Parse using simdjson
            simdjson::dom::parser parser;
            simdjson::dom::element doc;
            auto error = parser.parse(payload_ptr.data() + json_pos, payload_ptr.length() - json_pos).get(doc);

            if (error == simdjson::SUCCESS) {
                _message = from_json(doc, vector<shared_ptr<const string> >());
            }
            return false;
        }
    }

    bool packet::accept(string& payload_ptr, vector<shared_ptr<const string> >& buffers)
    {
        char frame_char = _frame + '0';
        payload_ptr.append(&frame_char, 1);

        if (_frame != frame_message) {
            return false;
        }

        bool hasMessage = false;
        string json_str;

        if (_message) {
            accept_message(*_message, json_str, buffers, true);
            hasMessage = true;
        }

        bool hasBinary = buffers.size() > 0;
        _type = _type & (~type_undetermined);

        if (_type == type_event) {
            _type = hasBinary ? type_binary_event : type_event;
        }
        else if (_type == type_ack) {
            _type = hasBinary ? type_binary_ack : type_ack;
        }

        ostringstream ss;
        ss.precision(8);
        ss << _type;

        if (hasBinary) {
            ss << buffers.size() << "-";
        }

        if (_nsp.size() > 0 && _nsp != "/") {
            ss << _nsp;
            if (hasMessage || _pack_id >= 0) {
                ss << ",";
            }
        }

        if (_pack_id >= 0) {
            ss << _pack_id;
        }

        payload_ptr.append(ss.str());

        if (hasMessage) {
            payload_ptr.append(json_str);
        }

        return hasBinary;
    }

    packet::frame_type packet::get_frame() const
    {
        return _frame;
    }

    packet::type packet::get_type() const
    {
        assert((_type & type_undetermined) == 0);
        return (type)_type;
    }

    string const& packet::get_nsp() const
    {
        return _nsp;
    }

    message::ptr const& packet::get_message() const
    {
        return _message;
    }

    unsigned packet::get_pack_id() const
    {
        return _pack_id;
    }

    void packet_manager::set_decode_callback(function<void(packet const&)> const& decode_callback)
    {
        m_decode_callback = decode_callback;
    }

    void packet_manager::set_encode_callback(function<void(bool, shared_ptr<const string> const&)> const& encode_callback)
    {
        m_encode_callback = encode_callback;
    }

    void packet_manager::reset()
    {
        m_partial_packet.reset();
    }

    void packet_manager::encode(packet& pack, encode_callback_function const& override_encode_callback) const
    {
        shared_ptr<string> ptr = make_shared<string>();
        vector<shared_ptr<const string> > buffers;
        const encode_callback_function* cb_ptr = &m_encode_callback;

        if (override_encode_callback) {
            cb_ptr = &override_encode_callback;
        }

        if (pack.accept(*ptr, buffers)) {
            if ((*cb_ptr)) {
                (*cb_ptr)(false, ptr);
            }
            for (auto it = buffers.begin(); it != buffers.end(); ++it) {
                if ((*cb_ptr)) {
                    (*cb_ptr)(true, *it);
                }
            }
        }
        else {
            if ((*cb_ptr)) {
                (*cb_ptr)(false, ptr);
            }
        }
    }

    void packet_manager::put_payload(string const& payload)
    {
        unique_ptr<packet> p;
        do {
            if (packet::is_text_message(payload)) {
                p.reset(new packet());
                if (p->parse(payload)) {
                    m_partial_packet = std::move(p);
                }
                else {
                    break;
                }
            }
            else if (packet::is_binary_message(payload)) {
                if (m_partial_packet) {
                    if (!m_partial_packet->parse_buffer(payload)) {
                        p = std::move(m_partial_packet);
                        break;
                    }
                }
            }
            else {
                p.reset(new packet());
                p->parse(payload);
                break;
            }
            return;
        } while (0);

        if (m_decode_callback) {
            m_decode_callback(*p);
        }
    }
}
