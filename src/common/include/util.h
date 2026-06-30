#pragma once
#include <string>
#include <sstream>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/access.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include "spdlog/fmt/fmt.h"

#define MY_NODISCARD [[nodiscard]]

class Operation {
  public:
    std::string key;
    std::string value;
    std::string client_id;
    int request_id;

  private:
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & key;
        ar & value;
        ar & client_id;
        ar & request_id;
    }

  public:
    /**字符串序列化 */
    [[nodiscard]] std::string SerializeToString() const {
        std::stringstream ss;
        boost::archive::text_oarchive toa(ss);
        toa << *this;
        return ss.str();
    }
    // 字符串反序列化
    void ParseFromString(const std::string& str) {
        std::stringstream ss(str);
        boost::archive::text_iarchive tia(ss);
        tia >> *this;
    }
    friend std::ostream& operator<<(std::ostream& os, const Operation& obj) {
        os << "---------------\n";
        std::string res = fmt::format("key:{0}\nvalue:{1}\nclient_id:{2}\nrequest_id:{3}\n", obj.key, obj.value,
                                      obj.client_id, obj.request_id);
        os << res;
        os << "---------------\n";
        return os;
    }
};